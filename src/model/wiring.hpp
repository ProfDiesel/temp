#pragma once

#include "../common/config_reader.hpp"
#include "../common/properties_dispatch.hpp"
#include "../common/socket.hpp"
#include "../feed/feed.hpp"
#include "../trigger/trigger_dispatcher.hpp"
#include "automata.hpp"
#include "payload.hpp"


#include <boilerplate/chrono.hpp>
#include <boilerplate/leaf.hpp>
#include <boilerplate/logger.hpp>
#include <boilerplate/outcome.hpp>
#include <boilerplate/piped_continuation.hpp>
#include <boilerplate/pointers.hpp>

#include <asio/awaitable.hpp>
#include <asio/buffer.hpp>
#include <asio/co_spawn.hpp>
#include <asio/connect.hpp>
#if defined(BACKTEST_HARNESS)
#include <asio/defer.hpp>
#endif // defined(BACKTEST_HARNESS)
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/posix/stream_descriptor.hpp>
#include <asio/read_until.hpp>
#include <asio/write.hpp>

#include <boost/hof/fix.hpp>
#include <boost/hof/partial.hpp>

#include <fmt/compile.h>
#include <fmt/format.h>

#include <std_function/function.h>

#include <algorithm>
#include <array>
#include <cstdio>
#if !defined(__clang__)
#  include <experimental/memory>
#endif
#include <memory>
#include <type_traits>
#include <unistd.h>

#if defined(BACKTEST_HARNESS)
namespace backtest
{
using snapshot_requester_type = std::function<asio::awaitable<out::result<feed::instrument_state>>(feed::instrument_id_type)>;
using update_source_type = std::function<boost::leaf::result<void>(std::function<void(network_clock::time_point, const asio::const_buffer &)>)>;
using send_stream_type = std::function<void(const asio::const_buffer &)>;

snapshot_requester_type make_snapshot_requester();
update_source_type make_update_source();
send_stream_type make_stream_send();

void delay(std::chrono::steady_clock::duration, std::function<void(void)>);
} // namespace backtest
#endif // defined(BACKTEST_HARNESS)


[[using gnu: flatten]] auto run(const config::properties::walker &config, asio::io_context &service, auto &command_input,
         auto &command_output, boilerplate::not_null_observer_ptr<logger::logger> logger, auto &&continuation,
         auto dynamic_subscription, auto &&trigger, auto handle_packet_loss,
         auto send_datagram) noexcept -> boost::leaf::result<void>
{
  using namespace config::literals;

  const auto feed = config["feed"_hs];
  const auto send = config["send"_hs];
  const auto subscription = config["subscription"_hs];
#if defined(USE_TCPDIRECT)
  static_stack stack;
#endif // defined(USE_TCPDIRECT)

  auto spawn = [&](auto coroutine) {
    asio::co_spawn(
      service,
      [&]() noexcept -> asio::awaitable<void> {
        auto result = co_await coroutine();
        if(!result)
        {
          using namespace logger::literals;
          logger->log(logger::critical, "{}. Leaving ..."_format, result.assume_error());
          service.stop();
        }
      },
      asio::detached);
  };

#if !defined(__clang__)
  using automata_type = automata<decltype(dynamic_subscription)::value, decltype(handle_packet_loss)::value, std::decay_t<decltype(trigger)>,
                                 decltype(send_datagram)::value>; // TODO: GCC 10 ICEs on this
#else
  using automata_type = automata<dynamic_subscription(), handle_packet_loss(), std::decay_t<decltype(trigger)>, send_datagram()>;
#endif

  //
  //
  // REQUEST SNAPSHOT
  //

  auto request_snapshot = ({
#if defined(BACKTEST_HARNESS)
      const auto requester = backtest::make_snapshot_requester();
#else //  defined(BACKTEST_HARNESS)
    const auto [snapshot_host, snapshot_port] = (config::address)from_walker(feed["snapshot"_hs]);
    const auto snapshot_endpoints = BOOST_LEAF_EC_TRY(asio::ip::tcp::resolver(service).resolve(snapshot_host, snapshot_port, _));
    auto snapshot_socket = asio::ip::tcp::socket(service);
    BOOST_LEAF_EC_TRY(asio::connect(snapshot_socket, snapshot_endpoints, _));
#endif // defined(BACKTEST_HARNESS)

    [&](typename automata_type::automaton *automaton) noexcept -> asio::awaitable<out::result<void>> {
      if constexpr(handle_packet_loss)
      {
        if(automaton->snapshot_request_running) [[unlikely]]
          co_return out::success();
        automaton->snapshot_request_running = true;
      }

#if defined(BACKTEST_HARNESS)
      auto state = OUTCOME_CO_TRYX(co_await requester(automaton->instrument_id));
#else //  defined(BACKTEST_HARNESS)
      auto state = OUTCOME_CO_TRYX(co_await feed::request_snapshot(snapshot_socket, automaton->instrument_id));
#endif // defined(BACKTEST_HARNESS)
      automaton->trigger.reset(std::move(state));
      if constexpr(handle_packet_loss)
      {
        automaton->sequence_id = state.sequence_id;
        automaton->snapshot_request_running = false;
      }

      co_return out::success();
    };
  });

  //
  //
  // AUTOMATA
  //

  automata_type automata = BOOST_LEAF_TRYX([&]() noexcept -> boost::leaf::result<automata_type> {
    if constexpr(!dynamic_subscription())
    {
      auto payload = BOOST_LEAF_TRYX(decode_payload<send_datagram()>(subscription));
      auto automaton =
        typename automata_type::automaton {.trigger = std::move(trigger), .payload = std::move(payload), .instrument_id = subscription["instrument"_hs]};
      spawn([&]() noexcept -> asio::awaitable<out::result<void>> {
        OUTCOME_CO_TRY(co_await request_snapshot(&automaton));
        service.stop();
        co_return out::success();
      });
      service.run();
      service.restart();
      return automata_type(logger, std::move(automaton));
    }
    else
      return automata_type(logger);
  }());

  //
  //
  // COMMANDS
  //

  const auto commands = [&](auto &command_input) noexcept -> asio::awaitable<out::result<void>> {
    std::string command_buffer;
    command_buffer.clear();

    auto handlers = std::make_tuple([&](const boost::leaf::error_info &unmatched) { return std::make_error_code(std::errc::not_supported); });

    for(;;)
    {
      auto bytes_transferred
        = OUTCOME_CO_TRYX(co_await asio::async_read_until(command_input, asio::dynamic_buffer(command_buffer), "\n\n", as_result(asio::use_awaitable)));

      if(!bytes_transferred)
        co_return out::failure(std::make_error_code(std::errc::io_error));

      using namespace dispatch::literals;
      const auto properties = OUTCOME_CO_TRYX(boilerplate::leaf_to_outcome([&]() noexcept { return config::properties::create(command_buffer); }, handlers));
      const auto entrypoint = properties["entrypoint"_hs];
      switch(dispatch_hash(entrypoint["type"_hs])) // TODO
      {
      case "payload"_h:
        automata.update_payload(entrypoint["instrument"_hs], OUTCOME_CO_TRYX(boilerplate::leaf_to_outcome(
                                                               [&]() noexcept { return decode_payload<automata_type::send_datagram>(entrypoint); }, handlers)));
        break;
      case "subscribe"_h:
        if constexpr(automata_type::dynamic_subscription)
        {
          OUTCOME_CO_TRY(co_await with_trigger(entrypoint, logger, [&](auto &&trigger_map) noexcept -> asio::awaitable<void> {
            auto trigger
              = polymorphic_trigger_dispatcher::make<trigger_dispatcher<std::decay_t<decltype(trigger_map)>>>(std::forward<decltype(trigger_map)>(trigger_map));
            auto automaton = typename automata_type::automaton {
              .trigger = std::move(trigger),
              .payload = OUTCOME_CO_TRYX(boilerplate::leaf_to_outcome([&]() noexcept { return decode_payload<automata_type::send_datagram>(entrypoint); }, handlers)),
              .instrument_id = entrypoint["instrument"_hs]};
            OUTCOME_CO_TRY(co_await request_snapshot(&automaton));
            automata.track(automaton.instrument_id, std::move(automaton));
          }));
        }
        break;
      case "unsubscribe"_h:
        if constexpr(automata_type::dynamic_subscription)
          automata.untrack(entrypoint["instrument"_hs]);
        break;
      case "quit"_h: service.stop(); break;
      }
    }
  };

  //
  //
  //  RECEIVE
  //

  auto receive = ({
#if defined(BACKTEST_HARNESS)
    auto updates_source = backtest::make_update_source();
#else
    const auto [updates_host, updates_port] = (config::address)from_walker(feed["update"_hs]);
    auto updates_source = BOOST_LEAF_TRYX(
#  if defined(USE_TCPDIRECT)
      multicast_udp_reader::create(service, stack, updates_host, updates_port)
#  elif defined(LINUX)
      multicast_udp_reader::create(service, updates_host, updates_port, from_walker(feed["spin_duration"_hs]))
#  else
      multicast_udp_reader::create(service, updates_host, updates_port)
#  endif // defined(USE_TCPDIRECT)
    );
#endif   // defined(BACKTEST_HARNESS)

    const auto spin_count = std::min(std::size_t(feed["spin_count"_hs]), std::size_t(1));

    [updates_source = std::move(updates_source), spin_count](auto &&continuation) mutable noexcept {
      for(auto n = spin_count; n; --n)
        (std::ref(updates_source) |= continuation)();
    };
  });

  //
  //
  // DECODE
  //

  const auto decode = ({
    const auto decode_header = [&](feed::instrument_id_type instrument_id, feed::sequence_id_type sequence_id) noexcept {
     typename automata_type::automaton *const automaton = automata.instrument(instrument_id);

      if constexpr(automata_type::handle_packet_loss)
        if(LIKELY(automaton) && UNLIKELY(++automaton->sequence_id != sequence_id))
          request_snapshot(automaton);

      return automaton;
    };

    //hof::partial(feed::decode)(decode_header);
    [&decode_header](auto &&continuation, const network_clock::time_point &timestamp, const asio::const_buffer &buffer) noexcept {
      return feed::decode(decode_header, continuation, timestamp, buffer);
    };
  });

  //
  //
  // TRIGGER
  //

  const auto trigger_ = [](auto &&continuation, const network_clock::time_point &feed_timestamp, feed::update &&update, typename automata_type::automaton *automaton) noexcept {
    return (automaton->trigger)(continuation, feed_timestamp, std::forward<decltype(update)>(update), automaton);
  };

  //
  //
  // SEND
  //

  auto send_ = ({
    auto send_socket = BOOST_LEAF_TRYX([&]() {
      if constexpr(send_datagram())
      {
        const auto [send_host, send_port] = (config::address)from_walker(send["datagram"_hs]);
#if defined(USE_TCPDIRECT)
        return udp_writer::create(service, stack, send_host, send_port);
#else
        return udp_writer::create(service, send_host, send_port);
#endif // defined(USE_TCPDIRECT)
      }
      else
        return boost::leaf::result<boilerplate::empty> {};
    }());
#if defined(BACKTEST_HARNESS)
    auto stream_send = backtest::make_stream_send();
#else  // defined(BACKTEST_HARNESS)
    auto send_stream = asio::posix::stream_descriptor(service, ::dup(send["fd"_hs]));
    auto stream_send = [&, send_stream = std::move(send_stream)](const auto &buffer) mutable { asio::write(send_stream, buffer); };
#endif // defined(BACKTEST_HARNESS)

    const bool disposable_payload = send["disposable_payload"_hs];
    const std::chrono::steady_clock::duration cooldown = from_walker(send["cooldown"_hs]);

    [&, send_socket = std::move(send_socket), stream_send = std::move(stream_send), disposable_payload, cooldown](const network_clock::time_point &feed_timestamp, typename automata_type::automaton *instrument,
                                                                                                                  auto send_for_real) mutable noexcept {
      network_clock::time_point send_timestamp {};
      if constexpr(send_datagram())
      {
        if constexpr(!send_for_real())
        {
          send_socket.send_blank(instrument->payload.datagram_payload);
          return false;
        }

        send_timestamp = send_socket.send(instrument->payload.datagram_payload);
      }
      stream_send(asio::const_buffer(instrument->payload.stream_payload));

      using namespace logger::literals;
      logger->log(logger::info, "Sent instrument:{} in:{} out:{}"_format, instrument->instrument_id, to_timespec(feed_timestamp), to_timespec(send_timestamp));

      if(disposable_payload)
      {
        static std::array<char, 64> buffer;
        constexpr auto request_payload = FMT_COMPILE("\
request.type <- request_payload; \n\
request.instrument = {}\n\n");
        const auto [_, size] = fmt::format_to_n(buffer.data(), sizeof(buffer), request_payload, instrument->instrument_id);
        spawn([&, size = size]() noexcept -> asio::awaitable<out::result<void>> {
          auto n = OUTCOME_CO_TRYX(co_await asio::async_write(command_output, asio::buffer(buffer.data(), size), as_result(asio::use_awaitable)));
#if !defined(__clang__)
          if(n != size) [[unlikely]]
            co_return out::failure(std::errc::not_supported);
#endif // !defined(__clang__)
          co_return out::success();
        });
      }

      auto leave_cooldown_token = automata.enter_cooldown(instrument);
      // whatever the value of error_code, get out of the cooldown state
#if defined(BACKTEST_HARNESS)
      backtest::delay(cooldown, [=, &service]() { asio::defer(service, leave_cooldown_token); });
#else // defined(BACKTEST_HARNESS)
      asio::steady_timer(service, cooldown).async_wait([=]([[maybe_unused]] auto error_code) { leave_cooldown_token(); });
#endif // defined(BACKTEST_HARNESS)
      return true;
    };
  });

  //
  //
  // WARM-UP
  //

  const auto warm_up = [&]() noexcept { automata.warm_up([&](typename automata_type::automaton *automaton) { send_(network_clock::time_point {}, automaton, std::false_type {}); }); };

  //
  //
  // PIPE THINGS TOGETHER
  //

  spawn([&]() noexcept -> asio::awaitable<out::result<void>> { return commands(command_input); });

  auto with_mca_markers = [](auto &&continuation) noexcept {
    asm volatile("# LLVM-MCA-BEGIN trigger");
    continuation();
    asm volatile("# LLVM-MCA-END trigger");
  };

  using namespace piped_continuation;
  return continuation([&]() noexcept {
    warm_up();
    with_mca_markers(std::ref(receive) |= decode |= trigger_ |= std::ref(send_));
  });
}


auto with_trigger_path(const config::properties::walker &config, asio::io_context &service, auto &command_input,
                       auto &command_output, boilerplate::not_null_observer_ptr<logger::logger> logger,
                       auto continuation) noexcept
{
  using namespace config::literals;

#if !defined(LEAN_AND_MEAN) && !defined(FUZZ_TEST_HARNESS)
  const auto dynamic_subscription_test = [&](auto &&continuation, auto &&...tags) noexcept {
    const auto subscription = config["subscription"_hs];
    return subscription ? with_trigger(subscription, logger,
                                       [&](auto &&trigger_dispatcher) {
                                         return continuation(std::forward<decltype(tags)>(tags)..., std::false_type {}, std::move(trigger_dispatcher));
                                       })()
                        : continuation(std::forward<decltype(tags)>(tags)..., std::true_type {}, polymorphic_trigger_dispatcher());
  };

  const auto handle_packet_loss_test = [&](auto &&continuation, auto &&...tags) noexcept {
    return config["feed"_hs]["handle_packet_loss"_hs] ? continuation(std::forward<decltype(tags)>(tags)..., std::true_type {})
                                                      : continuation(std::forward<decltype(tags)>(tags)..., std::false_type {});
  };

  const auto send_datagram_test = [&](auto &&continuation, auto &&...tags) noexcept {
    return config["send"_hs]["datagram"_hs] ? continuation(std::forward<decltype(tags)>(tags)..., std::true_type {})
                                            : continuation(std::forward<decltype(tags)>(tags)..., std::false_type {});
  };

  using namespace piped_continuation;
  return dynamic_subscription_test |= handle_packet_loss_test |= send_datagram_test
         |= hof::partial(run)(service, command_input, command_output, logger, continuation, properties);
#else  // !defined(LEAN_AND_MEAN) && !defined(FUZZ_TEST_HARNESS)
  const auto subscription = config["subscription"_hs];
  return with_trigger(subscription, logger, [&](auto &&trigger_dispatcher) {
    return run(config, service, command_input, command_output, logger, continuation, std::false_type {},
               std::forward<decltype(trigger_dispatcher)>(trigger_dispatcher), std::false_type {}, std::false_type {});
  });
#endif // !defined(LEAN_AND_MEAN) && !defined(FUZZ_TEST_HARNESS)
}

