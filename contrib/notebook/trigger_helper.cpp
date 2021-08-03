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
  template <typename exception_type>
  void throw_exception(const exception_type &exception)
  {
    boost::throw_exception(exception);
  }
} // namespace asio::detail
#endif // defined(ASIO_NO_EXCEPTIONS)

extern "C" polymorphic_trigger_dispatcher *_make_trigger(const char *config, char **error_message)
{
  using namespace config::literals;

  return boost::leaf::try_handle_all(
      [&]() noexcept -> boost::leaf::result<polymorphic_trigger_dispatcher*> {
        const auto props = BOOST_LEAF_TRYX(config::properties::create(std::string_view(config)));
        auto walker = props["entrypoint"_hs];
        return new polymorphic_trigger_dispatcher(BOOST_LEAF_TRYX(make_polymorphic_trigger(walker)));
      },
      [&](const config::parse_error &error, const boost::leaf::e_source_location &location) noexcept {
        if(error_message)
        {
          *error_message = new char[256];
          fmt::format_to_n(*error_message, 256, FMT_COMPILE("{}: At char {}-{}, expected a '{}' expression, got \"{}\"."), location, error.indices.first, error.indices.second, error.which, error.snippet);
        }
        return nullptr;
      },
      [&](const boost::leaf::error_info &unmatched, const std::error_code &error_code, const boost::leaf::e_source_location &location) noexcept {
        if(error_message)
        {
          *error_message = new char[256];
          fmt::format_to_n(*error_message, 256, FMT_COMPILE("{}: error code {} - {}."), location, error_code, fmt::to_string(unmatched));
        }
        return nullptr;
      },
      [&](const boost::leaf::error_info &unmatched, const boost::leaf::e_source_location &location) noexcept {
        if(error_message)
        {
          *error_message = new char[256];
          fmt::format_to_n(*error_message, 256, FMT_COMPILE("{}: {}."), location, fmt::to_string(unmatched));
        }
        return nullptr;
      },
      [&](const boost::leaf::error_info &unmatched) noexcept {
        if(error_message)
        {
          *error_message = new char[256];
          fmt::format_to_n(*error_message, 256, FMT_COMPILE("{}."), fmt::to_string(unmatched));
        }
        return nullptr;
      });
}

extern "C" void _release_error_message(char *error_message)
{
  delete []error_message;
}

extern "C" bool _on_update(polymorphic_trigger_dispatcher *trigger, std::int64_t timestamp, const feed::update *update)
{
  return (*trigger)([&](const std::chrono::steady_clock::time_point &timestamp, void *closure, bool for_real) { return for_real; }, std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(timestamp)), *update, nullptr);
}

extern "C" void _release_trigger(polymorphic_trigger_dispatcher *ptr)
{
  delete ptr;
}

#if defined(TEST)
// GCOVR_EXCL_START
#include <boost/ut.hpp>

namespace ut = boost::ut;

ut::suite trigger_helper = [] {
  using namespace ut;

  "simple"_test = [] {
    constexpr auto config = "\
entrypoint.instant_threshold <- 2;\n\
entrypoint.threshold <- 3;\n\
entrypoint.period <- 10;";
    wrapped::trigger trigger;
    expect(nothrow([&]() { trigger = wrapped::trigger(config); }));

    expect(!trigger(0, feed::encode_update(feed::field::b0, 0)));
    expect(!trigger(1, feed::encode_update(feed::field::b0, 1)));
    expect(trigger(2, feed::encode_update(feed::field::b0, 10)));
  };
};

int main() {}

// GCOVR_EXCL_STOP
#endif // defined(TEST)
