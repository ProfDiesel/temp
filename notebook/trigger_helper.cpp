#include "trigger_helper.hpp"

#include "common/config_reader.hpp"
#include "feed/feed.hpp"
#include "trigger/trigger_dispatcher.hpp"

#include <boilerplate/logger.hpp>

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

namespace feed::detail
{
template price_t read_value<price_t>(const struct update &update) noexcept;
} // namespace feed

extern "C" trigger_ptr make_trigger(const std::string &config)
{
  using namespace config::literals;
  using namespace logger::literals;

  logger::printer printer;
  logger::logger logger(boilerplate::make_strict_not_null(&printer));

  return boost::leaf::try_handle_all(
    [&]() -> boost::leaf::result<trigger_ptr> {

      const auto props = BOOST_LEAF_TRYX(config::properties::create(config));
      auto walker = props["entrypoint"_hs];
      return {std::make_shared<polymorphic_trigger_dispatcher>(BOOST_LEAF_TRYX(make_polymorphic_trigger(walker, boilerplate::make_strict_not_null(&logger))))};
    },
    [&](const config::parse_error &error, const boost::leaf::e_source_location &location) {
      logger.log(logger::critical, "{}: At char {}-{}, expected a '{}' expression, got \"{}\"."_format, location, error.indices.first, error.indices.second,
                  error.which, error.snippet);
      return nullptr;
    },
    [&](const boost::leaf::error_info &unmatched, const std::error_code &error_code, const boost::leaf::e_source_location &location) {
      logger.log(logger::critical, "{}: error code {} - {}"_format, location, error_code, fmt::to_string(unmatched));
      return nullptr;
    },
    [&](const boost::leaf::error_info &unmatched, const boost::leaf::e_source_location &location) {
      logger.log(logger::critical, "{}: {}"_format, location, fmt::to_string(unmatched));
      return nullptr;
    },
    [&](const boost::leaf::error_info &unmatched) {
      logger.log(logger::critical, fmt::to_string(unmatched));
      return nullptr;
    });
}

extern "C" bool run(trigger_ptr trigger, std::int64_t timestamp, feed::update update, const std::function<bool(std::int64_t, void *, bool)> &f)
{
  return (*trigger)([&](const clock::time_point &timestamp, void *closure, bool for_real) { return f(timestamp.time_since_epoch().count(), closure, for_real); }, clock::time_point(clock::duration(timestamp)), update, nullptr);
}
