#include "feed/feed_binary.hpp"
#include "model/wiring.hpp"

#include "fuzz_test_harness.hpp"

#include <boilerplate/pointers.hpp>

#include <asio/defer.hpp>
#include <asio/io_context.hpp>
#include <asio/posix/stream_descriptor.hpp>

#include <boost/container/flat_map.hpp>
#include <boost/leaf/common.hpp>
#include <boost/leaf/handle_errors.hpp>

#include <chrono>
#include <functional>
#include <string_view>

#if defined(BOOST_NO_EXCEPTIONS)
namespace boost
{
void throw_exception(const std::exception &exception) { std::abort(); }
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

using namespace std::chrono_literals;

namespace fuzz
{
// deterministic executor
class executor
{
public:
  using action_type = std::function<void(void)>;

  static std::unique_ptr<executor> instance;

  void add(const std::chrono::steady_clock::duration &delay, const action_type &action) { actions.emplace(nano_clock::now() + delay, action); }

  void poll()
  {
    if(actions.empty())
      return;

    const auto first = actions.begin(), last = actions.upper_bound(nano_clock::now() = first->first);
    std::for_each(first, last, [&](const auto &value) { value.second(); });
    actions.erase(first, last);
  }

private:
  static constexpr std::chrono::steady_clock::duration granularity = 1us;
  boost::container::flat_multimap<nano_clock::time_point, action_type> actions;
};

std::unique_ptr<executor> executor::instance;

struct feeder
{
  fuzz::generator gen;
  network_clock::time_point current_timestamp;
  std::uniform_int_distribution<network_clock::rep> clock_distribution {0, 1'000'000'000};

  static std::unique_ptr<feeder> instance;

  asio::awaitable<out::result<feed::instrument_state>> on_snapshot_request(feed::instrument_id_type instrument) noexcept
  {
    co_return out::failure(std::make_error_code(std::errc::io_error)); // TODO
  }

  boost::leaf::result<void> on_update_poll(std::function<void(network_clock::time_point, asio::const_buffer &&)> continuation) noexcept
  {
    std::aligned_storage<feed::detail::message_max_size, alignof(feed::message)> buffer_storage;
    std::byte *const first = reinterpret_cast<std::byte *>(&buffer_storage), *last = first, *const end = first + sizeof(buffer_storage);

    for(;;)
    {
      const auto filled = gen.fill(asio::buffer(last, end - last));
      last += filled.size();

      current_timestamp += network_clock::duration(clock_distribution(gen));

      auto buffer = asio::buffer(first, last - first);
      const auto sanitized = feed::detail::sanitize(boilerplate::overloaded {
                                                      [&](auto field, feed::price_t value) { return value; },
                                                      [&](auto field, feed::quantity_t value) { return value; },
                                                    },
                                                    buffer);
      continuation(current_timestamp, buffer);

      last = std::copy(first + sanitized, last, first);
    }
  }

  auto make_snapshot_requester() noexcept
  {
    return [this](feed::instrument_id_type instrument) noexcept { return on_snapshot_request(instrument); };
  }

  auto make_update_source() noexcept
  {
    return [this](std::function<void(network_clock::time_point, const asio::const_buffer &)> continuation) noexcept { return on_update_poll(continuation); };
  }
};

std::unique_ptr<feeder> feeder::instance;

struct stream_send
{
  auto operator()(const asio::const_buffer &buffer) noexcept
  {
    return out::success(network_clock::time_point {});
  }
};

} // namespace fuzz

namespace backtest
{
using snapshot_requester_type = std::function<asio::awaitable<out::result<feed::instrument_state>>(feed::instrument_id_type)>;
using update_source_type = std::function<boost::leaf::result<void>(std::function<void(network_clock::time_point, const asio::const_buffer &)>)>;

snapshot_requester_type make_snapshot_requester() { return fuzz::feeder::instance->make_snapshot_requester(); }
update_source_type make_update_source() { return fuzz::feeder::instance->make_update_source(); }
send_stream_type make_stream_send() { return fuzz::stream_send {}; }

using delayed_action = std::function<void(void)>;
void delay([[maybe_unused]] asio::io_context &service, const std::chrono::steady_clock::duration &delay, delayed_action action) { return fuzz::executor::instance->add(delay, action); }

} // namespace backtest

auto main() -> int
{
  using namespace std::string_view_literals;
  using namespace logger::literals;

  asio::io_context service(1);
  asio::posix::stream_descriptor command_input(service), command_output(service);

  logger::printer printer {};
  logger::logger logger {boilerplate::make_strict_not_null(&printer)};

  return boost::leaf::try_handle_all(
    [&]() -> boost::leaf::result<int> {
      using namespace config::literals;

      const auto config = ""sv;
      const auto properties = BOOST_LEAF_TRYX(config::properties::create(config));

      auto run = with_trigger_path(properties["config"_hs], service, command_input, command_output, boilerplate::make_strict_not_null(&logger),
                                         [&](auto fast_path) -> boost::leaf::result<void> {
                                           while(!service.stopped())
                                             [[likely]]
                                             {
                                               fast_path();
                                               fuzz::executor::instance->poll();
                                               if(service.poll())
                                                 bad_test(); // the non-deterministic executor is not supposed to be used
                                               logger.flush();
                                             }
                                           return {};
                                         });

      BOOST_LEAF_CHECK(run());
      return 0;
    },
    [&](const boost::leaf::error_info &unmatched) -> int { std::abort(); });
}
