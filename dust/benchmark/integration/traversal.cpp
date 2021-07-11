#include <initializer_list>
#define BACKTEST_HARNESS
#include "model/wiring.hpp"

#include "feed/feed.hpp"
#include "feed/binary/feed_server.hpp"

#include <benchmark/benchmark.h>

#include <boilerplate/leaf.hpp>
#include <boilerplate/logger.hpp>
#include <boilerplate/pointers.hpp>

#include <asio/io_context.hpp>
#include <asio/posix/stream_descriptor.hpp>
#include <asio/this_coro.hpp>

#include <boost/leaf/common.hpp>
#include <boost/leaf/handle_errors.hpp>

#include <fmt/format.h>

#include <std_function/function.h>

#include <chrono>
#include <iostream>
#include <string_view>

#include <unistd.h>

#if defined(BOOST_NO_EXCEPTIONS)
namespace boost
{
void throw_exception(const std::exception &exception)
{
  logger::printer printer;
  printer(0, logger::level::CRITICAL, exception.what());
  std::abort();
}
} // namespace boost
#endif //  defined(BOOST_NO_EXCEPTIONS)

#if defined(ASIO_NO_EXCEPTIONS)
namespace asio::detail
{
template<typename exception_type>
void throw_exception(const exception_type &exception)
{
  boost::throw_exception(exception);
}
} // namespace asio::detail
#endif // defined(ASIO_NO_EXCEPTIONS)

namespace bench
{
struct message
{
  feed::instrument_id_type instrument;
  feed::instrument_state state;
};

struct feeder
{
  feed::server server;
  network_clock::time_point timestamp {};
  std::vector<std::optional<std::vector<char>>> canned_updates {};

  static std::unique_ptr<feeder> instance;

  auto make_snapshot_requester() noexcept
  {
    return [this](feed::instrument_id_type instrument) noexcept { return on_snapshot_request(instrument); };
  }

  auto make_update_source() noexcept
  {
    return [this](func::function<void(network_clock::time_point, const asio::const_buffer &)> continuation) noexcept { return on_update_poll(continuation); };
  }

  boost::leaf::awaitable<boost::leaf::result<feed::instrument_state>> on_snapshot_request(feed::instrument_id_type instrument) noexcept
  {
    using namespace std::chrono_literals;
    co_await asio::steady_timer(co_await asio::this_coro::executor, 100ms).async_wait(boost::leaf::use_awaitable);
    co_return boost::leaf::success(server.snapshot(instrument));
  }

  [[using gnu: always_inline, flatten, hot]] inline boost::leaf::result<void>
  on_update_poll(func::function<void(network_clock::time_point, const asio::const_buffer &)> continuation) noexcept
  {
    timestamp += network_clock::duration(1);
    if(!canned_updates.empty())
      if(const auto &update = canned_updates[timestamp.time_since_epoch().count() % canned_updates.size()]; update)
        continuation(timestamp, asio::buffer(*update));
    return {};
  }

  void reset(feed::instrument_id_type instrument, const feed::instrument_state &state) noexcept { server.reset(instrument, state); }

  void push_updates(std::initializer_list<std::optional<message>> messages)
  {
    for(auto &&message: messages)
    {
      if(message)
      {
        std::vector<char> buffer(64);
        feed::detail::encode_message(message->instrument, message->state, asio::buffer(buffer.data(), buffer.size()));
        canned_updates.push_back(std::make_optional(buffer));
      }
      else
        canned_updates.push_back(std::nullopt);
    }
  }
};
std::unique_ptr<feeder> feeder::instance;

struct stream_send
{
  void operator()(const asio::const_buffer &buffer) noexcept
  {
    benchmark::DoNotOptimize(buffer);
  }
};
} // namespace bench

namespace backtest
{

snapshot_requester_type make_snapshot_requester() { return bench::feeder::instance->make_snapshot_requester(); }
update_source_type make_update_source() { return bench::feeder::instance->make_update_source(); }
send_stream_type make_stream_send() { return bench::stream_send {}; }

using delayed_action = func::function<void(void)>;
void delay([[maybe_unused]] asio::io_context &service, const std::chrono::steady_clock::duration &delay, delayed_action action) { asio::steady_timer(service, delay).async_wait([=]([[maybe_unused]] auto error_code) { action(); }); }
} // namespace backtest

static void traversal(benchmark::State &state) noexcept
{
  using namespace std::string_view_literals;

  constexpr auto initial_config = "\
config.feed <- 'feed';\n\
config.send <- 'send';\n\
config.subscription <- 'subscription';\n\
feed.snapshot <- '127.0.0.1:3287'; \n\
feed.spin_count <- 1; \n\
send.cooldown <- 2000000;\n\
send.disposable_payload <- 'true';\n\
subscription.instrument <- 1;\n\
subscription.instant_threshold <- 0.5;\n\
subscription.message <- 'bWVzc2FnZQo=';\n\
"sv;

  int command_in_pipe[2], command_out_pipe[2];
  ::pipe(command_in_pipe);
  ::pipe(command_out_pipe);

  asio::io_context service(1);
  asio::posix::stream_descriptor command_input(service, command_in_pipe[0]), command_output(service, command_out_pipe[1]);

  logger::printer printer {};
  logger::logger logger {boilerplate::make_strict_not_null(&printer)};

  auto error_handlers = std::make_tuple(
    [&](const boost::leaf::error_info &unmatched, const std::error_code &error_code, const boost::leaf::e_source_location &location)
    { std::clog << location << error_code << unmatched << std::endl; },
    [&](const boost::leaf::error_info &unmatched, const boost::leaf::e_source_location &location) { std::clog << location << unmatched << std::endl; },
    [&](const boost::leaf::error_info &unmatched) { std::clog << unmatched << std::endl; });

  std::shared_ptr<boost::leaf::polymorphic_context> ctx = boost::leaf::make_shared_context(error_handlers);

  using namespace feed::literals;

  bench::feeder::instance.reset(new bench::feeder {feed::server(service, asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), 3287),
                                                                asio::ip::udp::endpoint(asio::ip::make_address("239.255.0.1"), 3288))});
  const auto _ = gsl::finally([&]() { bench::feeder::instance.reset(); });

  bench::feeder::instance->reset(42, feed::instrument_state {.b0 = 95.0_p, .bq0 = 10, .o0 = 105.0_p, .oq0 = 10});
  bench::feeder::instance->push_updates(
    {std::nullopt,
     std::make_optional<bench::message>({.instrument = 42, .state = feed::instrument_state {.b0 = 95.0_p, .bq0 = 10, .o0 = 105.0_p, .oq0 = 10}})});

  boost::leaf::try_handle_all(
    [&]() -> boost::leaf::result<void>
    {
      using namespace config::literals;

      const auto properties = BOOST_LEAF_TRYX(config::properties::create(initial_config));

      auto run = with_trigger_path(properties["config"_hs], service, command_input, command_output, boilerplate::make_strict_not_null(&logger),
                                         [&](auto fast_path) -> boost::leaf::result<void>
                                         {
                                           // warm up with for a few cycles
                                           for(auto n = 0; n < 10; ++n)
                                           {
                                             if(service.stopped())
                                               return BOOST_LEAF_NEW_ERROR("stopped");
                                             fast_path();
                                             service.poll();
                                             logger.flush();
                                           }

                                           for(auto _: state)
                                           {
                                             fast_path();
                                             benchmark::ClobberMemory();
                                           }

                                           return {};
                                         });
      BOOST_LEAF_CHECK(run());
      return {};
    },
    error_handlers);
}

BENCHMARK(traversal);

BENCHMARK_MAIN();
