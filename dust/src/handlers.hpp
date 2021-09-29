#pragma once

#include "config/config_reader.hpp"
#include "trigger/trigger_dispatcher.hpp"

#include <boilerplate/fmt.hpp>
#include <boilerplate/logger.hpp>
#include <boilerplate/mem_utils.hpp>

#if defined(__clang__) 
#include <boost/container/pmr/polymorphic_allocator.hpp>
#include <boost/container/pmr/unsynchronized_pool_resource.hpp>
#endif

#include <boost/leaf/capture.hpp>
#include <boost/leaf/error.hpp>

#include <cstdlib>
#if !defined(__clang__) 
#include <memory_resource>
#endif
#include <system_error>
#include <tuple>

template<typename value_type>
struct escape_double_quotes_wrapper
{
  value_type value;
};

auto escape_double_quotes(auto &&value)
{
#if !defined(NDEBUG)
  proc_maps::instance().check(std::forward<decltype(value)>(value));
#endif

  return escape_double_quotes_wrapper<std::decay_t<decltype(value)>> {.value = std::forward<decltype(value)>(value)};
}

template<typename value_type, typename char_type>
struct fmt::formatter<escape_double_quotes_wrapper<value_type>, char_type> : fmt::formatter<value_type, char_type>
{
  template<typename context_type>
  auto format(const escape_double_quotes_wrapper<value_type> &escapist, context_type &context)
  {
    std::string result;
    boost::algorithm::replace_all_copy(std::back_inserter(result), escapist.value, "\"", "\\\"");
    return formatter<value_type, char_type>::format(result, context);
  }
};


inline auto make_handlers(auto &&printer) noexcept
{
  using namespace logger::literals;

  return std::tuple {[printer](const config::parse_error &error, const boost::leaf::e_source_location &location) noexcept
                     {
                       return printer("location=\"{}:{} {}\" first={} last={} expected=\"{}\" actual=\"{}\" Parse error"_format, location.file,
                               location.line, location.function, error.indices.first, error.indices.second, escape_double_quotes(error.which), escape_double_quotes(error.snippet));
                     },
                     [printer](const invalid_trigger_config &invalid_trigger_config, const boost::leaf::e_source_location &location) noexcept
                     {
                       return printer("location=\"{}:{} {}\" config={} Invalid trigger config"_format, location.file, location.line,
                               location.function, static_cast<const void *>(invalid_trigger_config.walker.object.get()));
                     },
                     [printer](const missing_field &missing_field, const boost::leaf::e_source_location &location) noexcept
                     {
                       return printer("location=\"{}:{} {}\" field={} Missing field"_format, location.file, location.line, location.function,
                               missing_field.field.data());
                     },
                     [printer](const std::error_code &error_code, const boost::leaf::e_source_location &location, boilerplate::statement statement) noexcept
                     {
                       return printer("location=\"{}:{} {}\" statement=\"{}\", code={} {}"_format, location.file, location.line, location.function, 
                               escape_double_quotes(static_cast<std::string_view>(statement)), error_code.value(), error_code.message());
                     },
                     [printer](const std::error_code &error_code, const boost::leaf::e_source_location &location) noexcept
                     {
                       return printer("location=\"{}:{} {}\" code={} {}"_format, location.file, location.line, location.function, error_code.value(),
                               error_code.message());
                     },
                     [printer](const boost::leaf::e_source_location &location) noexcept
                     {
                       return printer("location=\"{}:{} {}\""_format, location.file, location.line, location.function);
                     },
                     [printer](const std::error_code &error_code) noexcept
                     {
                       return printer("code={} {}"_format, error_code.value(), error_code.message());
                     },
                     [printer](const boost::leaf::error_info &unmatched) noexcept
                     {
                       return printer("{}"_format, fmt::to_string(unmatched));
                     }};
}

inline auto to_string_handlers() noexcept
{
  return make_handlers([]([[maybe_unused]] auto format, auto &&...args) noexcept { 
      std::array<char, 1'024> buffer {};
      const auto [_, size] = fmt::format_to_n(buffer.data(), buffer.size(), FMT_COMPILE(decltype(format)::buffer), std::forward<decltype(args)>(args)...);
      return std::string(buffer.data(), size);
  });
}

/*

auto get_context_allocator(auto &&...handlers)
{
  using context_type = boost::leaf::context_type_from_handlers<decltype(handlers)...>;
#if defined(__clang__) 
  return boost::container::pmr::polymorphic_allocator<context_type>(boost::container::pmr::get_default_resource());
#else
  namespace pmr = std::prm;
  static std::pmr::unsynchronized_pool_resource resource({.max_blocks_per_chunk = 0, .largest_required_pool_block = 0});
  return std::pmr::polymorphic_allocator<context_type>(&resource);
#endif
}

inline auto pack_result(auto &&result) noexcept requires boost::leaf::is_result_type<std::decay_t<decltype(result)>>::value
{
  auto handlers = to_string_handlers();
  return boost::leaf::capture(boost::leaf::allocate_shared_context(get_context_allocator(handlers), handlers), [&]() { return std::move(result); });
} 

template<typename value_type, typename char_type>
struct fmt::formatter<const boost::leaf::result<value_type>&, char_type> : fmt::formatter<value_type, char_type>
{
  template<typename context_type>
  auto format(const boost::leaf::result<value_type> &result, context_type &context)
  {
    using namespace std::string_literals;
    const auto as_string = boost::leaf::try_handle_all([&]() noexcept -> boost::leaf::result<std::string> {
      BOOST_LEAF_CHECK(std::move(result)); 
      return ""s;
    }, to_string_handlers());
    return formatter<value_type, char_type>::format(as_string, context);
  }
};

*/
inline auto pack_result(auto &&result) noexcept requires boost::leaf::is_result_type<std::decay_t<decltype(result)>>::value
{
  using namespace std::string_literals;
  return boost::leaf::try_handle_all([&]() noexcept -> boost::leaf::result<std::string> {
    BOOST_LEAF_CHECK(std::move(result)); 
    return ""s;
  }, to_string_handlers());
} 

