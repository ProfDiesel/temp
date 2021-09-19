#include "handlers.hpp"
#include "model/automata.hpp"

#include <boilerplate/chrono.hpp>
#include <boilerplate/logger.hpp>
#include <boilerplate/likely.hpp>
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
#include <asio/signal_set.hpp>
#include <asio/write.hpp>

#include <boost/core/noncopyable.hpp>

#include <boost/leaf/common.hpp>
#include <boost/leaf/handle_errors.hpp>

#include <fmt/compile.h>
#include <fmt/format.h>
#include <fmt/ostream.h>

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


#if defined(BOOST_NO_EXCEPTIONS)
namespace boost
{
/*[[noreturn]]*/ void throw_exception(const std::exception &exception)
{
  logger::printer printer;
  printer(logger::level::CRITICAL, exception.what());
  std::abort();
}

struct source_location;
/*[[noreturn]]*/ void throw_exception(std::exception const &e, boost::source_location const &) { throw_exception(e); }
} // namespace boost
#endif //  defined(BOOST_NO_EXCEPTIONS)

#if defined(ASIO_NO_EXCEPTIONS)
namespace asio::detail
{
template<typename exception_type>
/*[[noreturn]]*/ void throw_exception(const exception_type &exception)
{
  boost::throw_exception(exception);
}
} // namespace asio::detail
#endif // defined(ASIO_NO_EXCEPTIONS)

struct logger_thread : boost::noncopyable
{
  logger::printer printer {};
  logger::logger logger {boilerplate::make_strict_not_null(&printer)};
  std::atomic_bool leave {};
  static_assert(decltype(leave)::is_always_lock_free);

  std::thread thread {[this]()
                      {
                        while(!leave.load(std::memory_order_acquire))
                          logger.drain();
                      }};

  ~logger_thread()
  {
    logger.flush();
    logger.drain();
    leave.store(true, std::memory_order_release);
    thread.join();
  }
};


auto co_commands(asio::io_context &service, asio::posix::stream_descriptor &command_input, auto co_request_snapshot, auto &automata, boilerplate::not_null_observer_ptr<logger::logger> logger_ptr) noexcept -> boost::leaf::awaitable<boost::leaf::result<void>>
{
  using automata_type = typename std::decay_t<decltype(automata)>;

  constexpr bool send_datagram = automata_type::automaton_type::send_datagram;
  constexpr bool dynamic_subscription = automata_type::dynamic_subscription;

  std::string command_buffer;
  command_buffer.clear();

  for(;;)
  {
    logger_ptr->log(logger::debug, "awaiting commands");
    auto bytes_transferred = BOOST_LEAF_ASIO_CO_TRYX(
      co_await asio::async_read_until(command_input, asio::dynamic_buffer(command_buffer), "\n\n", _));

    if(!bytes_transferred)
      co_return std::make_error_code(std::errc::io_error);

    using namespace config::literals;
    using namespace dispatch::literals;
    using namespace logger::literals;

    const auto properties = BOOST_LEAF_CO_TRYX(config::properties::create(command_buffer));
    const auto entrypoint = properties["entrypoint"_hs];
    logger_ptr->log(logger::debug, "command=\"{}\" command recieved"_format, entrypoint["type"_hs]);
    switch(dispatch_hash(entrypoint["type"_hs])) // TODO
    {
    case "payload"_h:
      if(auto *automaton_ptr = automata.at(entrypoint["instrument"_hs]); automaton_ptr)
        automaton_ptr->payload = BOOST_LEAF_CO_TRYX(decode_payload<send_datagram>(entrypoint));
      break;
    case "subscribe"_h:
      if constexpr(dynamic_subscription)
      {
        const auto instrument_id = entrypoint["instrument"_hs];
        auto state = BOOST_LEAF_CO_TRYX(co_await co_request_snapshot(instrument_id));
        BOOST_LEAF_CO_TRYV(with_trigger(entrypoint, logger_ptr, [&](auto &&trigger_map) noexcept -> boost::leaf::result<void> {
          auto trigger = polymorphic_trigger_dispatcher::make<trigger_dispatcher<std::decay_t<decltype(trigger_map)>>>(std::forward<decltype(trigger_map)>(trigger_map));
          trigger.reset(std::move(state));
          auto payload = BOOST_LEAF_TRYX(decode_payload<send_datagram>(entrypoint));
          automata.emplace({.instrument_id = instrument_id, .trigger = std::move(trigger), .payload = std::move(payload)});
          return boost::leaf::success();
        })());
      }
      break;
    case "unsubscribe"_h:
      if constexpr(dynamic_subscription)
        automata.erase(entrypoint["instrument"_hs]);
      break;
    case "quit"_h: service.stop(); break;
    }
  }
};


auto main() -> int
{
  using namespace config::literals;
  using namespace logger::literals;
  using namespace std::string_literals;

  asio::io_context service(1);

  asio::posix::stream_descriptor command_input(service, ::dup(STDIN_FILENO)), command_output(service, ::dup(STDOUT_FILENO));

  logger_thread logger_thread;
  auto logger_ptr = boilerplate::make_strict_not_null(&logger_thread.logger);

  logger_ptr->log(logger::info, "lwpid={} Starting."_format, std::this_thread::get_id());
  logger_ptr->flush();

  //
  // spawn

  auto spawn = [&service, logger_ptr](auto &&coroutine, auto name)
  {
    logger_ptr->log(logger::debug, "coroutine=\"{}\" spawned"_format, name);
    asio::co_spawn(
      service,
      [&]() noexcept -> boost::leaf::awaitable<void>
      {
        co_await boost::leaf::co_try_handle_all(
          [&]() noexcept -> boost::leaf::awaitable<boost::leaf::result<void>>
          {
            logger_ptr->log(logger::debug, "coroutine=\"{}\" started"_format, name);
            BOOST_LEAF_CO_TRYV(co_await std::forward<decltype(coroutine)>(coroutine)());
            logger_ptr->log(logger::debug, "coroutine=\"{}\" exited"_format, name);
      	    co_return boost::leaf::success();
          },
          [&](const std::error_code &error_code, const boost::leaf::e_source_location location, const std::string &statement) noexcept
          {
            logger_ptr->log(logger::critical, "coroutine=\"{}\" error_code={} error=\"{}\" file=\"{}\" line={} statement=\"{}\" exited"_format, name,
                        error_code.value(), error_code.message(), location.file, location.line, statement);
            service.stop();
          },
          [&](const boost::leaf::error_info &ei) noexcept
          {
            logger_ptr->log(logger::critical, "coroutine=\"{}\" leaf_error_id={} exited"_format, name, ei.error());
            service.stop();
          });
      },
      asio::detached);
  };


  //
  // arm signals

  asio::signal_set signals(service, SIGINT, SIGTERM);
  signals.async_wait(
    [&](auto error_code, auto signal_number)
    {
      if(error_code)
        return;
      logger_ptr->log(logger::info, "signal={} Interrupting."_format, signal_number);
      service.stop();
    });

  //
  // post-send

  auto post_send = [&](const auto &properties, auto &automata) {
    const auto send = properties["send"_hs];
    const bool disposable_payload = *send["disposable_payload"_hs];
    const std::chrono::steady_clock::duration cooldown = *send["cooldown"_hs];

    return [spawn, &service, &automata, &command_output, disposable_payload, cooldown](auto *instrument_ptr) {
      if(disposable_payload)
      {
        static std::array<char, 64> buffer;
        constexpr auto request_payload = FMT_COMPILE("\
  est.type <- request_payload; \n\
  est.instrument = {}\n\n");
        const auto [_, size] = fmt::format_to_n(buffer.data(), sizeof(buffer), request_payload, instrument_ptr->instrument_id);
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

      auto leave_cooldown_token = automata.enter_cooldown(instrument_ptr);
    // whatever the value of error_code, get out of the cooldown state
#if defined(BACKTEST_HARNESS)
      backtest::delay(service, cooldown, [=, &service]() { asio::defer(service, leave_cooldown_token); });
#else  // defined(BACKTEST_HARNESS)
      asio::steady_timer(service, cooldown).async_wait([=]([[maybe_unused]] auto error_code) { leave_cooldown_token(); });
#endif // defined(BACKTEST_HARNESS)

      return true;
    };
  };

  boost::leaf::try_handle_all([&]() noexcept -> boost::leaf::result<void> {
      using namespace config::literals;

      std::string command_buffer;
      BOOST_LEAF_EC_TRYV(asio::read_until(command_input, asio::dynamic_buffer(command_buffer), "\n\n", _));
      const auto properties = BOOST_LEAF_TRYX(config::properties::create(command_buffer));

      //
      // receive

#if defined(BACKTEST_HARNESS)
      auto co_request_snapshot = backtest::make_snapshot_requester();
      auto updates_socket = backtest::make_update_source();
#else // defined(BACKTEST_HARNESS)
      auto snapshot_socket = ({ 
          const auto [snapshot_host, snapshot_port] = (config::address)*properties["feed"_hs]["snapshot"_hs];
          const auto snapshot_endpoints = BOOST_LEAF_EC_TRYX(asio::ip::tcp::resolver(service).resolve(snapshot_host, snapshot_port, _));
          auto snapshot_socket = asio::ip::tcp::socket(service);
          BOOST_LEAF_EC_TRYV(asio::connect(snapshot_socket, snapshot_endpoints, _));
          std::move(snapshot_socket);
      });

      auto co_request_snapshot = [snapshot_socket = std::move(snapshot_socket), logger_ptr] (auto instrument_id) mutable noexcept -> boost::leaf::awaitable<boost::leaf::result<feed::instrument_state>> {
        REQUIRES(automaton);
        logger_ptr->log(logger::debug, "instrument=\"{}\" request snapshot"_format, instrument_id);
        auto state = BOOST_LEAF_CO_TRYX(co_await feed::co_request_snapshot(snapshot_socket, instrument_id));
        logger_ptr->log(logger::debug, "instrument=\"{}\" sequence_id={} received snapshot"_format, instrument_id, state.sequence_id);
        co_return state;
      };

      const auto [updates_host, updates_port] = (config::address)*properties["feed"_hs]["update"_hs];
#  if defined(LINUX) && !defined(USE_TCPDIRECT) && !defined(USE_LIBVMA)
      auto updates_socket = multicast_udp_reader::create(service, updates_host, updates_port, properties["feed"_hs]["spin_duration"_hs].get_or(1'000ns), properties["feed"_hs]["timestamping"_hs].get_or(false));
#  else
      auto updates_socket = multicast_udp_reader::create(service, updates_host, updates_port);
#  endif // defined(LINUX) && !defined(USE_TCPDIRECT) && !defined(USE_LIBVMA)
#endif // defined(BACKTEST_HARNESS)

      auto receive = [updates_socket = std::move(updates_socket), spin_count = std::min(std::size_t(properties["feed"_hs]["spin_count"_hs].get_or(1)), std::size_t(1))](auto continuation) mutable noexcept {
            using namespace piped_continuation;
            for(auto n = spin_count; n; --n)
              (std::ref(updates_socket) |= continuation)();
      };

      //
      // decode

      const auto decode = [&](auto &automata) {
        const auto decode_header = [&](feed::instrument_id_type instrument_id, feed::sequence_id_type sequence_id) noexcept
        {
          auto *const automaton_ptr = automata.at_if_not_disabled(instrument_id);
          auto snapshot_requester = [&]() { return [&](auto termination_handler) {
            spawn([&]() {
              auto state = BOOST_LEAF_CO_TRYX(co_await co_request_snapshot(automaton_ptr->instrument_id));
              automaton_ptr->apply(std::move(state));
            }, "request_snapshot"s);
          }; };
          return LIKELY(automaton_ptr) && LIKELY(automaton_ptr->handle_sequence_id(sequence_id, snapshot_requester)) ? automaton_ptr : nullptr;
        };

        return [decode_header](auto continuation, const network_clock::time_point &timestamp, const asio::const_buffer &buffer) noexcept { return feed::detail::decode(decode_header, continuation, timestamp, buffer); };
      };

      //
      // trigger

      const auto trigger = [](auto continuation, const network_clock::time_point &feed_timestamp, const feed::update &update, auto automaton_ptr) noexcept { return (automaton_ptr->trigger)(continuation, feed_timestamp, update, automaton_ptr); };

      //
      // send

      const auto [send_host, send_port] = (config::address)*properties["send"_hs]["datagram"_hs];
      auto send_datagram_socket = *properties["send"_hs] ? std::make_optional(BOOST_LEAF_TRYX(udp_writer::create(service, send_host, send_port))) : std::nullopt;

#if defined(BACKTEST_HARNESS)
      auto stream_send = backtest::make_stream_send();
#else  // defined(BACKTEST_HARNESS)
      auto send_stream = asio::posix::stream_descriptor(service, ::dup(properties["send"_hs]["fd"_hs]));
      auto stream_send = [send_stream = std::move(send_stream)](auto buffer) mutable noexcept { asio::write(send_stream, buffer); };
#endif // defined(BACKTEST_HARNESS)

      const auto send = [&](auto &automata) {
        constexpr bool send_datagram = std::decay_t<decltype(automata)>::automaton_type::send_datagram;

        return [&, send_datagram_socket = std::move(send_datagram_socket), stream_send = std::move(stream_send)](auto continuation, const network_clock::time_point &feed_timestamp, auto *instrument_ptr, auto send_for_real) mutable noexcept {
          network_clock::time_point send_timestamp {};
          if constexpr(send_datagram)
          {
            if constexpr(!send_for_real())
            {
              send_datagram_socket->send_blank(instrument_ptr->payload.datagram_payload);
              return false;
            }

            send_timestamp = send_datagram_socket->send(instrument_ptr->payload.datagram_payload);
          }
          stream_send(asio::const_buffer(instrument_ptr->payload.stream_payload));

          logger_ptr->log(logger::info, "instrument={} in_ts={} out_ts={} Payload sent"_format, instrument_ptr->instrument_id, to_timespec(feed_timestamp),
                      to_timespec(send_timestamp));

          return continuation(instrument_ptr);
        };
      };

      // 
      // main loop

      auto run = with_automata(properties["config"_hs], logger_ptr, [&](auto &&automata) -> boost::leaf::result<void> {

        if(!std::decay_t<decltype(automata)>::dynamic_subscription)
        {
          automata.each([&](auto &automaton) -> boost::leaf::result<void> {
            bool done = false;
            spawn([&]() -> boost::leaf::awaitable<boost::leaf::result<void>> { 
              auto state = BOOST_LEAF_CO_TRYX(co_await co_request_snapshot(automaton.instrument_id));
              automaton.trigger.reset(std::move(state));
              done = true;
              co_return boost::leaf::success();
            }, "initial snapshot"s);
            while(!done)
              BOOST_LEAF_EC_TRYV(service.poll(_));
            return boost::leaf::success();
          });
        } 

        spawn([&]() { return co_commands(service, command_input, std::ref(co_request_snapshot), automata, logger_ptr); }, "commands"s);
 
        using namespace piped_continuation;
        auto send_ = send(automata);
        auto f = decode(automata) |= trigger |= std::ref(send_) |= post_send(properties, automata);
        f(network_clock::time_point(), asio::const_buffer(nullptr, 0));
        /*
        auto fast_path = std::ref(receive) |= decode(automata) |= trigger |= std::ref(send_) |= post_send(properties, automata);
 
        while(!service.stopped())
        [[likely]]
        {
          // warm up
          automata.each([&](auto automaton) {
              automaton.trigger.warm_up();
              send_([](auto){}, network_clock::time_point {}, automaton, std::false_type {});
          });
          asm volatile("# LLVM-MCA-BEGIN trigger");
          fast_path();
          asm volatile("# LLVM-MCA-END trigger");

          BOOST_LEAF_EC_TRYV(service.poll(_));
          logger_ptr->flush();
        }
        logger_ptr->log(logger::info, "Executor stopped.");
        */
        return boost::leaf::success();
      });

      BOOST_LEAF_CHECK(run());

      return boost::leaf::success();
    },
    make_handlers(std::ref(logger_thread.printer)));

  return 0;
}
