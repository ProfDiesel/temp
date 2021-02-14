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

// ajouter un autre executor (deterministe) que l'on poll() avant le asio::io_context()
// --> push les timers dedans
// 1 tour de boucle = 100ns
// -> poste (defer(... asio::detached)) l'action au bout de n boucle

struct backtest_scheduler
{
  using backtest_clock = nano_clock;
  using action_type = std::function<void(void)>;

  // disable by preproc every other way to get a clock
  backtest_clock::time_point now() noexcept { return current_timestamp; }

  void add(const backtest_clock::duration &delay, const action_type &action) { actions.emplace(current_timestamp + delay, action); }

  void poll()
  {
    if(actions.empty())
      return;

    const auto first = actions.begin();
    current_timestamp = first->first;
    const auto last = actions.upper_bound(current_timestamp);
    std::for_each(first, last, [&](const auto &value) { value.second(); });
    actions.erase(first, last);
  }

private:
  static constexpr backtest_clock::duration granularity = 1us;
  backtest_clock::time_point current_timestamp {};
  boost::container::flat_multimap<backtest_clock::time_point, action_type> actions;
};

struct feeder
{
  fuzz::generator gen;
  network_clock::time_point current_timestamp;
  std::uniform_int_distribution<network_clock::rep> clock_distribution {0, 1'000'000'000};

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
      const auto sanitized = feed::detail::sanitize(
        boilerplate::overloaded {
          [&](auto field, feed::price_t value) { return value; },
          [&](auto field, feed::quantity_t value) { return value; },
        },
        buffer);
      continuation(current_timestamp, buffer);

      last = std::copy(first + sanitized, last, first);
    }
  }

  auto make_update_source() const noexcept { return std::bind(&feeder::on_update_poll, this); }

  static auto &instance() noexcept
  {
    static feeder instance;
    return instance;
  }
};

namespace invariant
{
// triggers
// jamais dans le buffer de situation de trigger (ou automaton en cooldown)

// cooldown doit etre résistant aux souscriptions dynamiques
} // namespace invariant

auto main() -> int
{
  using namespace std::string_view_literals;
  using namespace logger::literals;

  asio::io_context service(1);
  asio::posix::stream_descriptor command_input(service), command_output(service);

  backtest_scheduler backtest_scheduler;

  logger::printer printer {};
  logger::logger logger {boilerplate::make_strict_not_null(&printer)};

  return boost::leaf::try_handle_all(
    [&]() -> boost::leaf::result<int>
    {
      using namespace config::literals;

      const auto config = ""sv;
      const auto properties = BOOST_LEAF_TRYX(config::properties::create(config));

      const auto run = with_trigger_path(properties["config"_hs], service, command_input, command_output, boilerplate::make_strict_not_null(&logger),
                                         [&](auto fast_path) -> boost::leaf::result<void>
                                         {
                                           while(!service.stopped())
                                             [[likely]]
                                             {
                                               fast_path();
                                               backtest_scheduler.poll();
                                               service.poll();
                                               logger.flush();
                                             }
                                           return {};
                                         });

      BOOST_LEAF_CHECK(run());
      return 0;
    },
    [&](const boost::leaf::error_info &unmatched) -> int { std::abort(); });
}
