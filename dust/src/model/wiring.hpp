#pragma once

#include "../config/config_reader.hpp"
#include "../config/dispatch.hpp"
#include "../trigger/trigger_dispatcher.hpp"
#include "automata.hpp"
#include "payload.hpp"

#include <boilerplate/chrono.hpp>
#include <boilerplate/contracts.hpp>
#include <boilerplate/leaf.hpp>
#include <boilerplate/logger.hpp>
#include <boilerplate/likely.hpp>
#include <boilerplate/piped_continuation.hpp>
#include <boilerplate/pointers.hpp>
#include <boilerplate/socket.hpp>

#include <feed/feed.hpp>

#include <asio/awaitable.hpp>
#include <asio/buffer.hpp>
#include <asio/co_spawn.hpp>
#include <asio/connect.hpp>
#if defined(BACKTEST_HARNESS)
#  include <asio/defer.hpp>
#endif // defined(BACKTEST_HARNESS)
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/posix/stream_descriptor.hpp>
#include <asio/read_until.hpp>
#include <asio/write.hpp>

#if !defined(LEAN_AND_MEAN) && !defined(FUZZ_TEST_HARNESS)
#  include <boost/hof/partial.hpp>
#endif // !defined(LEAN_AND_MEAN) && !defined(FUZZ_TEST_HARNESS)

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
#include <string>
#include <type_traits>
#include <unistd.h>

#if defined(BACKTEST_HARNESS)
namespace backtest
{
using snapshot_requester_type = std::function<boost::leaf::awaitable<boost::leaf::result<feed::instrument_state>>(feed::instrument_id_type)>;
using update_source_type = std::function<boost::leaf::result<void>(std::function<void(const network_clock::time_point&, const asio::const_buffer &)>)>;
using send_stream_type = std::function<void(const asio::const_buffer &)>;

snapshot_requester_type make_snapshot_requester();
update_source_type make_update_source();
send_stream_type make_stream_send();

using delayed_action = func::function<void(void)>;
void delay([[maybe_unused]] asio::io_context &service, const std::chrono::steady_clock::duration &delay, delayed_action action);
} // namespace backtest
#endif // defined(BACKTEST_HARNESS)

[[using gnu: flatten]] auto run(const config::walker &config, asio::io_context &service, auto &command_input, auto &command_output,
                                boilerplate::not_null_observer_ptr<logger::logger> logger, auto continuation, auto dynamic_subscription, auto &&trigger,
                                auto handle_packet_loss, auto send_datagram) noexcept -> boost::leaf::result<void>
{
  using namespace config::literals;
  using namespace logger::literals;
  using namespace std::string_literals;

  const auto feed = config["feed"_hs];
  const auto send = config["send"_hs];
  const auto subscription = config["subscription"_hs];

  auto spawn = [&](auto &&coroutine, auto name)
  {
    logger->log(logger::debug, "coroutine=\"{}\" spawned"_format, name);
    asio::co_spawn(
      service,
      [&]() noexcept -> boost::leaf::awaitable<void>
      {
        co_await boost::leaf::co_try_handle_all(
          [&]() noexcept -> boost::leaf::awaitable<boost::leaf::result<void>>
          {
            logger->log(logger::debug, "coroutine=\"{}\" started"_format, name);
            BOOST_LEAF_CO_TRYV(co_await std::forward<decltype(coroutine)>(coroutine)());
            logger->log(logger::debug, "coroutine=\"{}\" exited"_format, name);
      	    co_return boost::leaf::success();
          },
          [&](const std::error_code &error_code, const boost::leaf::e_source_location location, const std::string &statement) noexcept
          {
            logger->log(logger::critical, "coroutine=\"{}\" error_code={} error=\"{}\" file=\"{}\" line={} statement=\"{}\" exited"_format, name,
                        error_code.value(), error_code.message(), location.file, location.line, statement);
            service.stop();
          },
          [&](const boost::leaf::error_info &ei) noexcept
          {
            logger->log(logger::critical, "coroutine=\"{}\" leaf_error_id={} exited"_format, name, ei.error());
            service.stop();
          });
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

  auto request_snapshot = (
    {
#if defined(BACKTEST_HARNESS)
      auto requester = backtest::make_snapshot_requester();
#else  //  defined(BACKTEST_HARNESS)
      const auto [snapshot_host, snapshot_port] = (config::address)*feed["snapshot"_hs];
      const auto snapshot_endpoints = BOOST_LEAF_EC_TRYX(asio::ip::tcp::resolver(service).resolve(snapshot_host, snapshot_port, _));
      auto snapshot_socket = asio::ip::tcp::socket(service);
      BOOST_LEAF_EC_TRYV(asio::connect(snapshot_socket, snapshot_endpoints, _));
#endif // defined(BACKTEST_HARNESS)

#if defined(BACKTEST_HARNESS)
      [requester = std::move(requester), handle_packet_loss, logger]
#else  //  defined(BACKTEST_HARNESS)
      [snapshot_socket = std::move(snapshot_socket), handle_packet_loss, logger]
#endif // defined(BACKTEST_HARNESS)
      (typename automata_type::automaton *automaton) mutable noexcept -> boost::leaf::awaitable<boost::leaf::result<void>>
      {
        REQUIRES(automaton);
        logger->log(logger::debug, "instrument=\"{}\" request snapshot"_format, automaton->instrument_id);
        if constexpr(handle_packet_loss)
        {
          if(automaton->snapshot_request_running) [[unlikely]]
            co_return boost::leaf::success();
          automaton->snapshot_request_running = true;
        }

#if defined(BACKTEST_HARNESS)
        auto state = BOOST_LEAF_CO_TRYX(co_await requester(automaton->instrument_id));
#else  //  defined(BACKTEST_HARNESS)
        auto state = BOOST_LEAF_CO_TRYX(co_await feed::request_snapshot(snapshot_socket, automaton->instrument_id));
#endif // defined(BACKTEST_HARNESS)
        logger->log(logger::debug, "instrument=\"{}\" sequence_if={} received snapshot"_format, automaton->instrument_id, state.sequence_id);
        automaton->trigger.reset(std::move(state));
        if constexpr(handle_packet_loss)
        {
          automaton->sequence_id = state.sequence_id;
          automaton->snapshot_request_running = false;
        }
        co_return boost::leaf::success();
      };
    });

  //
  //
  // AUTOMATA
  //

  auto instrument_id = subscription["trigger"_hs]["instrument"_hs];
  assert(instrument_id == 42);

  automata_type automata = BOOST_LEAF_TRYX(
    [&]() noexcept -> boost::leaf::result<automata_type>
    {
      if constexpr(!dynamic_subscription())
      {
        auto payload = BOOST_LEAF_TRYX(decode_payload<send_datagram()>(subscription["payload"_hs]));
        auto automaton =
          typename automata_type::automaton {.trigger = std::move(trigger), .payload = std::move(payload), .instrument_id = subscription["trigger"_hs]["instrument"_hs]};
        spawn(
          [&]() noexcept -> boost::leaf::awaitable<boost::leaf::result<void>>
          {
            BOOST_LEAF_CO_TRYV(co_await request_snapshot(&automaton));
            service.stop();
            co_return boost::leaf::success();
          },
          "initial snapshot"s);
        return automata_type(logger, std::move(automaton));
      }
      else
        return automata_type(logger);
    }());

  //
  //
  // COMMANDS
  //

  const auto commands = [&](auto &command_input) noexcept -> boost::leaf::awaitable<boost::leaf::result<void>>
  {
    std::string command_buffer;
    command_buffer.clear();

    for(;;)
    {
      logger->log(logger::debug, "awaiting commands");
      auto bytes_transferred = BOOST_LEAF_ASIO_CO_TRYX(
        co_await asio::async_read_until(command_input, asio::dynamic_buffer(command_buffer), "\n\n", _));

      if(!bytes_transferred)
        co_return std::make_error_code(std::errc::io_error);

      using namespace dispatch::literals;
      const auto properties = BOOST_LEAF_CO_TRYX(config::properties::create(command_buffer));
      const auto entrypoint = properties["entrypoint"_hs];
      logger->log(logger::debug, "command=\"{}\" command recieved"_format, entrypoint["type"_hs]);
      switch(dispatch_hash(entrypoint["type"_hs])) // TODO
      {
      case "payload"_h:
        automata.update_payload(entrypoint["instrument"_hs], BOOST_LEAF_CO_TRYX(decode_payload<automata_type::send_datagram>(entrypoint)));
        break;
      case "subscribe"_h:
        if constexpr(automata_type::dynamic_subscription)
        {
          BOOST_LEAF_CO_TRYV(co_await with_trigger(entrypoint, logger,
                                                  [&](auto &&trigger_map) noexcept -> boost::leaf::awaitable<void>
                                                  {
                                                    auto trigger
                                                      = polymorphic_trigger_dispatcher::make<trigger_dispatcher<std::decay_t<decltype(trigger_map)>>>(
                                                        std::forward<decltype(trigger_map)>(trigger_map));
                                                    auto automaton = typename automata_type::automaton
                                                    {
                                                      .trigger = std::move(trigger),
                                                      .payload = BOOST_LEAF_CO_TRYX(decode_payload<automata_type::send_datagram>(entrypoint)),
                                                      .instrument_id = entrypoint["instrument"_hs]
                                                    };
                                                    BOOST_LEAF_CO_TRYV(co_await request_snapshot(&automaton));
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

  auto receive = (
    {
#if defined(BACKTEST_HARNESS)
      auto update_source = backtest::make_update_source();
#else
      const auto [updates_host, updates_port] = (config::address)*feed["update"_hs];
      auto update_source = BOOST_LEAF_TRYX(
#  if defined(LINUX) && !defined(USE_TCPDIRECT) && !defined(USE_LIBVMA)
        multicast_udp_reader::create(service, updates_host, updates_port, feed["spin_duration"].get_or(1'000ns), feed["timestamping"].get_or(false))
#  else
        multicast_udp_reader::create(service, updates_host, updates_port)
#  endif // defined(LINUX) && !defined(USE_TCPDIRECT) && !defined(USE_LIBVMA)
      );
#endif   // defined(BACKTEST_HARNESS)

      const auto spin_count = std::min(std::size_t(feed["spin_count"_hs].get_or(1)), std::size_t(1));

      [update_source = std::move(update_source), spin_count](auto continuation) mutable noexcept
      {
        using namespace piped_continuation;
        for(auto n = spin_count; n; --n)
          (std::ref(update_source) |= continuation)();
      };
    });

  //
  //
  // DECODE
  //

  const auto decode = (
    {
      const auto decode_header = [&](feed::instrument_id_type instrument_id, feed::sequence_id_type sequence_id) noexcept
      {
        typename automata_type::automaton *const automaton = automata.instrument(instrument_id);

        if constexpr(automata_type::handle_packet_loss)
          if(LIKELY(automaton) && UNLIKELY(++automaton->sequence_id != sequence_id))
            request_snapshot(automaton);

        return automaton;
      };

      [&decode_header](auto continuation, const network_clock::time_point &timestamp, const asio::const_buffer &buffer) noexcept
      { return feed::detail::decode(decode_header, continuation, timestamp, buffer); };
    });

  //
  //
  // TRIGGER
  //

  const auto trigger_ =
    [](auto continuation, const network_clock::time_point &feed_timestamp, const feed::update &update, typename automata_type::automaton *automaton) noexcept
  { return (automaton->trigger)(continuation, feed_timestamp, update, automaton); };

  //
  //
  // SEND
  //

  auto send_ = (
    {
      auto send_socket = BOOST_LEAF_TRYX(
        [&]()
        {
          if constexpr(send_datagram())
          {
            const auto [send_host, send_port] = (config::address)*send["datagram"_hs];
            return udp_writer::create(service, send_host, send_port);
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
      const std::chrono::steady_clock::duration cooldown = *send["cooldown"_hs];

      [&, send_socket = std::move(send_socket), stream_send = std::move(stream_send), disposable_payload,
       cooldown](const network_clock::time_point &feed_timestamp, typename automata_type::automaton *instrument, auto send_for_real) mutable noexcept
      {
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

        logger->log(logger::info, "instrument={} in_ts={} out_ts={} Payload sent"_format, instrument->instrument_id, to_timespec(feed_timestamp),
                    to_timespec(send_timestamp));

        if(disposable_payload)
        {
          static std::array<char, 64> buffer;
          constexpr auto request_payload = FMT_COMPILE("\
request.type <- request_payload; \n\
request.instrument = {}\n\n");
          const auto [_, size] = fmt::format_to_n(buffer.data(), sizeof(buffer), request_payload, instrument->instrument_id);
          spawn(
            [&, size = size]() noexcept -> boost::leaf::awaitable<boost::leaf::result<void>> {
              auto n = BOOST_LEAF_ASIO_CO_TRYX(
                co_await asio::async_write(command_output, asio::buffer(buffer.data(), size), _));
#if !defined(__clang__)
              if(n != size) [[unlikely]]
                co_return std::errc::not_supported;
#endif // !defined(__clang__)
              co_return boost::leaf::success();
            },
            "request payload"s);
        }

        auto leave_cooldown_token = automata.enter_cooldown(instrument);
      // whatever the value of error_code, get out of the cooldown state
#if defined(BACKTEST_HARNESS)
        backtest::delay(service, cooldown, [=, &service]() { asio::defer(service, leave_cooldown_token); });
#else  // defined(BACKTEST_HARNESS)
        asio::steady_timer(service, cooldown).async_wait([=]([[maybe_unused]] auto error_code) { leave_cooldown_token(); });
#endif // defined(BACKTEST_HARNESS)
        return true;
      };
    });

  //
  //
  // WARM-UP
  //

  const auto warm_up = [&]() noexcept
  { automata.warm_up([&](typename automata_type::automaton *automaton) { send_(network_clock::time_point {}, automaton, std::false_type {}); }); };

  //
  //
  // PIPE THINGS TOGETHER
  //

  spawn([&]() noexcept -> boost::leaf::awaitable<boost::leaf::result<void>> { return commands(command_input); }, "commands"s);

  using namespace piped_continuation;
  return continuation(
    [&]() noexcept
    {
      warm_up();
      asm volatile("# LLVM-MCA-BEGIN trigger");
      (std::ref(receive) |= decode |= trigger_ |= std::ref(send_))();
      asm volatile("# LLVM-MCA-END trigger");
    });
}

auto with_trigger_path(const config::walker &config, asio::io_context &service, auto &command_input, auto &command_output,
                       boilerplate::not_null_observer_ptr<logger::logger> logger, auto continuation) noexcept
{
  using namespace config::literals;

#if !defined(LEAN_AND_MEAN) && !defined(FUZZ_TEST_HARNESS)
  const auto dynamic_subscription_test = [&](auto continuation, auto &&...tags) noexcept
  {
    const auto trigger = config["subscription"_hs]["trigger"_hs];
    return trigger ? with_trigger(trigger, logger,
                                       [&](auto &&trigger_dispatcher)
                                       { return continuation(std::forward<decltype(tags)>(tags)..., std::false_type {}, std::move(trigger_dispatcher)); })()
                        : continuation(std::forward<decltype(tags)>(tags)..., std::true_type {}, polymorphic_trigger_dispatcher());
  };

  const auto handle_packet_loss_test = [&](auto continuation, auto &&...tags) noexcept
  {
    return config["feed"_hs]["handle_packet_loss"_hs] ? continuation(std::forward<decltype(tags)>(tags)..., std::true_type {})
                                                      : continuation(std::forward<decltype(tags)>(tags)..., std::false_type {});
  };

  const auto send_datagram_test = [&](auto continuation, auto &&...tags) noexcept
  {
    return config["send"_hs]["datagram"_hs] ? continuation(std::forward<decltype(tags)>(tags)..., std::true_type {})
                                            : continuation(std::forward<decltype(tags)>(tags)..., std::false_type {});
  };

  using namespace piped_continuation;
  return dynamic_subscription_test |= handle_packet_loss_test |= send_datagram_test
         |= hof::partial(run)(service, command_input, command_output, logger, continuation, properties);
#else  // !defined(LEAN_AND_MEAN) && !defined(FUZZ_TEST_HARNESS)
  const auto trigger = config["subscription"_hs]["trigger"_hs];
  return with_trigger(trigger, logger,
                      [&](auto &&trigger_dispatcher)
                      {
                        return run(config, service, command_input, command_output, logger, continuation, std::false_type {},
                                   std::forward<decltype(trigger_dispatcher)>(trigger_dispatcher), std::false_type {}, std::false_type {});
                      });
#endif // !defined(LEAN_AND_MEAN) && !defined(FUZZ_TEST_HARNESS)
}
