#include "model/wiring.hpp"

#include "fuzz_test_harness.hpp"

#include <asio/io_context.hpp>
#include <asio/posix/stream_descriptor.hpp>

#include <boost/leaf/common.hpp>
#include <boost/leaf/handle_errors.hpp>

#include <chrono>

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

constexpr auto INITIAL_VALUE = feed::price_t {};
constexpr auto THRESHOLD = feed::price_t {1};
constexpr auto PERIOD = 1s;

// ajouter un autre executor (deterministe) que l'on poll() avant le asio::io_context()
// --> push les timers dedans
// 1 tour de boucle = 100ns
// -> poste (defer(... asio::detached)) l'action au bout de n boucle

namespace bench
{
struct feeder
{
  auto make_update_source() const noexcept { return std::bind(on_update_poll, this); }

  HOTPATH leaf::result<void> on_update_poll(std::function<void(clock::time_point, asio::const_buffer &&)> continuation) noexcept
  {
    std::array<char, feed::message_max_size> buffer_storage;
    auto *current = buffer.data();

    for(;;)
    {
      ::read(fd, current, buffer.data() + buffer.size() - current);
      auto remaining = feed::sanitize(
        boilerplate::overloaded {
          [&](auto field, feed::price_t value) { return value; },
          [&](auto field, feed::quantity_t value) { return value; },
        },
        asio::buffer(buffer.data(), buffer.size()));

      continuation(timestamp, buffer);

      current = std::copy_n(remaining.data(), remaining.size(), buffer.data());
    }
  }

  static auto &instance() noexcept
  {
    static feeder instance;
    return instance;
  }
};

auto main() -> int
{
  using namespace logger::literals;

  asio::io_context service(1);
  asio::posix::stream_descriptor input(service, ::dup(STDIN_FILENO));
  asio::posix::stream_descriptor command_input, command_output;

  {
    // build instrument 0 state
    // build instrument 1 state
    while(gen_data_available)
    {
      mutate_state();
      broadcast_state();
    }
  }

  return leaf::try_handle_all(
    [&]() -> leaf::result<int>
    {
      BOOST_LEAF_CHECK((
        {
        move_trigger<feed::price_t> trigger(INITIAL_VALUE, THRESHOLD, PERIOD);
          auto continuation = [&](auto fast_path) -> leaf::result<void>
          {
          while(LIKELY(!service.stopped()))
          {
            fast_path();
              backtest_executor.poll();
            service.poll();
            logger->flush();
          }
          return {};
        };
        return run(service, command_input, command_output, logger, continuation, config::properties {}, std::false_type {}, std::move(trigger_dispatcher),
                   std::false_type {}, std::false_type {});
      }));
      return 0;
    },
    [&](const leaf::error_info &unmatched) { std::abort(); });
}

