#include "model/wiring.hpp"

#include <boilerplate/logger.hpp>
#include <boilerplate/pointers.hpp>

#include <asio/io_context.hpp>
#include <asio/posix/stream_descriptor.hpp>
#include <asio/signal_set.hpp>

#include <boost/core/noncopyable.hpp>

#include <boost/leaf/common.hpp>
#include <boost/leaf/handle_errors.hpp>

#include <fmt/ostream.h>

#include <atomic>
#include <thread>

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

struct logger_thread : boost::noncopyable
{
  logger::printer printer {};
  logger::logger logger {boilerplate::make_strict_not_null(&printer)};
  std::atomic_bool leave {};
  static_assert(decltype(leave)::is_always_lock_free);

  std::thread thread {[this]() {
    while(!leave.load(std::memory_order_acquire))
      logger.drain();
  }};

  ~logger_thread()
  {
    logger.flush();
    leave.store(true, std::memory_order_release);
    thread.join();
  }
};

auto main() -> int
{
  using namespace logger::literals;

  asio::io_context service(1);

  asio::posix::stream_descriptor command_input(service, ::dup(STDIN_FILENO)), command_output(service, ::dup(STDOUT_FILENO));

  logger_thread logger_thread;
  auto logger = boilerplate::make_strict_not_null(&logger_thread.logger);

  asio::signal_set signals(service, SIGINT, SIGTERM);
  signals.async_wait([&](auto error_code, auto signal_number) {
    if(error_code)
      return;
    logger->log(logger::info, "Caught signal {}. Interrupting."_format, signal_number);
    service.stop();
  });

  return leaf::try_handle_all(
    [&]() -> leaf::result<int> {
      using namespace config::literals;

      std::string command_buffer;
      BOOST_LEAF_EC_TRY(asio::read_until(command_input, asio::dynamic_buffer(command_buffer), "\n\n", _));
      const auto properties = BOOST_LEAF_TRYX(config::properties::create(command_buffer));

      const auto run = with_trigger_path(properties["config"_hs], service, command_input, command_output, logger, [&](auto fast_path) -> leaf::result<void> {
        while(!service.stopped())
          [[likely]]
          {
            fast_path();
            service.poll();
            logger->flush();
          }
        return {};
      });

      BOOST_LEAF_CHECK(run());
      return 0;
    },
    [&](const config::parse_error &error, const leaf::e_source_location &location) {
      logger->log(logger::critical, "{}: At char {}-{}, expected a '{}' expression, got \"{}\"."_format, location, error.indices.first, error.indices.second,
                  error.which, error.snippet);
      return 1;
    },
    [&](const leaf::error_info &unmatched, const std::error_code &error_code, const leaf::e_source_location &location) {
      logger->log(logger::critical, "{}: error code {} - {}"_format, location, error_code, fmt::to_string(unmatched));
      return 2;
    },
    [&](const leaf::error_info &unmatched, const leaf::e_source_location &location) {
      logger->log(logger::critical, "{}: {}"_format, location, fmt::to_string(unmatched));
      return 3;
    },
    [&](const leaf::error_info &unmatched) {
      logger->log(logger::critical, fmt::to_string(unmatched));
      return 4;
    });
}

