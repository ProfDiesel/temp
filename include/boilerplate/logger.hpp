#pragma once

#include <boilerplate/boilerplate.hpp>
#include <boilerplate/fmt.hpp>
#include <boilerplate/pointers.hpp>
#include <boilerplate/spsc_ring_buffer.hpp>

#if defined(DEBUG)
#  include <boilerplate/mem_utils.hpp>
#endif // defined(DEBUG)

#include <boost/core/noncopyable.hpp>

#include <fmt/compile.h>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <fmt/ranges.h>

#include <memory>
#include <numeric>
#include <string>
#include <thread>
#include <variant>

#if defined(LOGGER_SYSLOG_FORMAT)
#include <syslog.h>
#endif // defined(LOGGER_SYSLOG_FORMAT)

namespace logger
{
enum struct level : uint8_t
{
  DEBUG_,
  INFO,
  WARNING,
  ERROR,
  CRITICAL
};

static const auto debug = std::integral_constant<level, level::DEBUG_> {};
static const auto info = std::integral_constant<level, level::INFO> {};
static const auto warning = std::integral_constant<level, level::WARNING> {};
static const auto error = std::integral_constant<level, level::ERROR> {};
static const auto critical = std::integral_constant<level, level::CRITICAL> {};

template<char... chars>
struct format : std::integer_sequence<char, chars...>
{
  static constexpr char buffer[] = {chars..., '\0'};

  template<char... other_chars>
  constexpr format<chars..., other_chars...> operator+([[maybe_unused]] format<other_chars...> _) const
  {
    return {};
  }
};

namespace literals
{
template<typename char_type, char_type... chars>
constexpr format<chars...> operator""_format()
{
  return {};
}
} // namespace literals

template<typename>
constexpr auto is_format_v = false;

template<char... chars>
constexpr auto is_format_v<format<chars...>> = true;

struct printer
{
#if defined(LOGGER_SYSLOG_FORMAT)
  int facility = LOG_USER;

  static constexpr auto level_to_priority(level level) noexcept
  {
    switch(level)
    {
      case level::DEBUG_: return LOG_DEBUG;
      case level::INFO: return LOG_INFO;
      case level::WARNING: return LOG_WARNING;
      case level::ERROR: return LOG_ERR;
      case level::CRITICAL: return LOG_CRIT;
    }
  }

#else // defined(LOGGER_SYSLOG_FORMAT)

  static constexpr auto level_to_string(level level) noexcept
  {
    switch(level)
    {
    case level::DEBUG_: return "DEBUG";
    case level::INFO: return "INFO ";
    case level::WARNING: return "WARN.";
    case level::ERROR: return "ERROR";
    case level::CRITICAL: return "CRIT.";
    }
    return "";
  }

#endif // defined(LOGGER_SYSLOG_FORMAT)

#if defined(LOGGER_FMT_COMPILE)
  template<typename>
  static constexpr auto bracket()
  {
    using namespace literals;
    return "{}"_format;
  }

  template<typename first_arg_type, typename... args_types>
  void operator()(uint64_t tsc, level level, const first_arg_type &first_arg, const args_types &...args) noexcept
  {
    if constexpr(is_format_v<std::decay_t<first_arg_type>>)
    {
      std::array<char, 4'096> line {};
      using namespace literals;
#if defined(LOGGER_SYSLOG_FORMAT)
      const auto [out, size] = fmt::format_to_n(line.data(), sizeof(line) - 1, FMT_COMPILE(("<{}> {:016x} "_format + first_arg_type {} + "\n"_format).buffer), level_to_priority(level), tsc, args...);
#else // defined(LOGGER_SYSLOG_FORMAT)
      const auto [out, size] = fmt::format_to_n(line.data(), sizeof(line) - 1, FMT_COMPILE(("{:016x} {}"_format + first_arg_type {} + "\n"_format).buffer), tsc, level_to_string(level), args...);
#endif // defined(LOGGER_SYSLOG_FORMAT)
      *out = '\0';
      std::fwrite(line.data(), 1, size, stderr);
    }
    else
    {
      constexpr auto brackets = []() {
        if constexpr(!sizeof...(args))
          return bracket<first_arg_type>();
        else
          return bracket<first_arg_type>() + (bracket<args_types>() + ...);
      }();
      static_assert(is_format_v<std::decay_t<decltype(brackets)>>);
      (*this)(tsc, level, brackets, first_arg, args...);
    }
  }
#else  // defined(LOGGER_FMT_COMPILE)
  template<typename... args_types>
  void operator()(uint64_t tsc, level level, const args_types &...args) noexcept
  {
    fmt::print(stderr, /*FMT_COMPILE*/ ("{:016x} {} {}\n"), tsc, level_to_string(level), fmt::join(std::tuple(args...), ""));
  }
#endif // defined(LOGGER_FMT_COMPILE)
};

namespace detail
{
template<typename printer_type, level level, typename... args_types>
struct alignas(sizeof(void *)) payload
{
  using pop_t = void (*)(spsc_ring_buffer &, printer_type &);

  const uint64_t tsc = __rdtsc();
  const pop_t pop = do_pop;

  const std::tuple<std::decay_t<args_types>...> args;

  explicit payload(args_types &&...args) noexcept: args(std::forward<args_types>(args)...)
  {
    /*
    auto assert_is_trivial = [](auto a) {
      static_assert(std::is_trivially_default_constructible_v<decltype(a)>);
      static_assert(std::is_trivially_constructible_v<decltype(a)>);
      static_assert(std::is_trivially_destructible_v<decltype(a)>);
    };
    std::apply([assert_is_trivial](auto &&...args) { ((assert_is_trivial(args), ...)); }, this->args);
    */
#if defined(DEBUG)
    std::apply([](auto &&...args) { ((proc_maps::instance().check(std::forward<decltype(args)>(args)), ...)); }, this->args);
#endif
  }

  static void do_pop(spsc_ring_buffer &srb, printer_type &printer) noexcept
  {
    auto *thiz = reinterpret_cast<payload *>(srb.consumer_peek(sizeof(payload)));
    std::apply([&](auto &&...args) { printer(thiz->tsc, level, std::forward<decltype(args)>(args)...); }, thiz->args);
    thiz->~payload();
    srb.consumer_commit(sizeof(*thiz));
  }
};
} // namespace detail

template<typename printer_type, typename on_full_type>
class basic_logger
{
public:
  // NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
  explicit basic_logger(boilerplate::not_null_observer_ptr<printer_type> printer, spsc_ring_buffer::size_t size = 1'024 * 1'024,
                        spsc_ring_buffer::size_t max_push_size = 256) noexcept:
    queue_(size, max_push_size),
    printer_(printer)
  {
  }

  template<typename level_type, typename... args_types>
  void log(level_type level, args_types &&...args) noexcept /*requires std::is_same_v<typename level_type::value_type, enum level>*/
  {
    if constexpr(level() == level::DEBUG_)
    {
#if defined(DEBUG) || defined(BOILERPLATE_LOGGER_DEBUG)
      if(!debug_)
#endif // defined(DEBUG) || defined(BOILERPLATE_LOGGER_DEBUG)
        return;
    }
    using payload_t = detail::payload<printer_type, level(), args_types...>;
    static constexpr auto payload_size = sizeof(payload_t);

    payload_t *address = nullptr;
    do
    {
      address = reinterpret_cast<payload_t *>(queue_.producer_allocate(payload_size));
    } while(!address && on_full_(payload_size));

    if(UNLIKELY(!address))
      return;

#if defined(has_construct_at)
    std::construct_at(address, std::forward<args_types>(args)...);
#else
    new(address) payload_t(std::forward<args_types>(args)...);
#endif
    queue_.producer_commit(payload_size);
  }

  void flush() noexcept { queue_.producer_flush(); }

  void debug(bool debug) noexcept { debug_ = debug; }
  [[nodiscard]] auto debug() const noexcept { return debug_; }

  auto drain() noexcept
  {
    using payload_t = detail::payload<printer_type, logger::info>;
    auto *payload = reinterpret_cast<payload_t *>(queue_.consumer_peek(sizeof(payload_t)));
    if(!payload)
      return false;

    do
    {
      (*payload->pop)(queue_, *printer_);
    } while((payload = reinterpret_cast<payload_t *>(queue_.consumer_peek(sizeof(payload_t)))));
    queue_.consumer_flush();
    return true;
  }

private:
  bool debug_ = false;
  spsc_ring_buffer queue_;
  boilerplate::not_null_observer_ptr<printer_type> printer_;
  on_full_type on_full_ = {};
};

struct do_nothing
{
  auto operator()([[maybe_unused]] std::size_t _) const noexcept { return true; }
};

using logger = basic_logger<printer, do_nothing>;

//
//
// ostream-like interface
//

template<typename printer_type, typename on_full_type>
struct ostreamlike_basic_logger : basic_logger<printer_type, on_full_type>
{
  template<typename... args_types>
  class builder : boost::noncopyable
  {
    friend struct ostreamlike_basic_logger;
    using logger_ptr = boilerplate::not_null_observer_ptr<ostreamlike_basic_logger>;

  public:
    builder(builder &&) noexcept = default;
    ~builder() noexcept
    {
      if constexpr(sizeof...(args_types) == 0U)
        return;

      if(!logger_)
        return;

      std::apply([this](auto &&...args) noexcept { logger_->log(std::forward<decltype(args)>(args)...); }, args_);
    }

    template<typename to_log_type>
    constexpr auto operator<<(to_log_type &&to_log) &&noexcept
    {
      return std::apply(
        [this, &to_log](auto &&...args) {
          return builder<args_types..., std::decay_t<decltype(to_log)>>(std::move(logger_), std::forward<decltype(args)>(args)...,
                                                                        std::forward<to_log_type>(to_log));
        },
        args_);
    }

  private:
    logger_ptr logger_ {};
    const std::tuple<args_types...> args_;

    builder() noexcept = default;

    constexpr explicit builder(logger_ptr &&logger, args_types... args) noexcept: logger_(std::move(logger)), args_(std::forward<args_types>(args)...) {}
  };

  auto log() noexcept { return builder<>(typename builder<>::logger_ptr(this)); }
};

// clang-format off
#define LOG(instance, level, to_log)   do { if((level) >= logger::global_log_level) { (instance).log() << (level) << to_log; } } while(false)
#define LOG_DEBUG_(instance, to_log)    LOG(instance, logger::level::DEBUG,    to_log)
#define LOG_INFO_(instance, to_log)     LOG(instance, logger::level::INFO,     to_log)
#define LOG_WARNING_(instance, to_log)  LOG(instance, logger::level::WARNING,  to_log)
#define LOG_ERROR_(instance, to_log)    LOG(instance, logger::level::ERROR,    to_log)
#define LOG_CRITICAL_(instance, to_log) LOG(instance, logger::level::CRITICAL, to_log)
// clang-format on

struct variant_buffer
{
  using element_type = std::variant<bool, int, long int, long long int, unsigned int, unsigned long int, unsigned long long int, float, double, long double,
                                    const void *, const char *, std::string>;
  using elements_type = std::vector<element_type>;

  elements_type elements_;

  template<typename to_log_type>
  auto &operator<<(to_log_type &&to_log) noexcept
  {
    elements_.emplace_back(std::forward<to_log_type>(to_log));
    return *this;
  }
};

} // namespace logger

template<>
struct fmt::formatter<logger::variant_buffer>
{
  constexpr auto parse(fmt::format_parse_context &context) noexcept { return context.end(); }

  template<typename context_type>
  auto format(const logger::variant_buffer &buffer, context_type &context) noexcept
  {
    return format_to(context.out(), "{}", fmt::join(buffer.elements_, ""));
  }
};

// multi-logger loop
namespace logger
{
struct logger_loop
{
  std::vector<std::reference_wrapper<logger>> loggers_ = {};

  void register_logger(logger &logger) noexcept { loggers_.push_back(std::ref(logger)); }

  bool operator()() noexcept
  {
    return std::accumulate(loggers_.begin(), loggers_.end(), false, [](auto acc, auto &logger) { return logger.get().drain() || acc; });
  }
};
} // namespace logger
