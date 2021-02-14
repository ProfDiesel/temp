#pragma once

#include <boost/type_index.hpp>

#include <fmt/chrono.h>
#include <fmt/compile.h>
#include <fmt/format.h>
#include <fmt/ostream.h>

#include <algorithm>
#include <functional>
#include <filesystem>
#include <thread>

template<typename value_type, typename char_type>
struct fmt::formatter<std::reference_wrapper<const value_type>, char_type> : fmt::formatter<value_type, char_type>
{
  template<typename context_type>
  auto format(const std::reference_wrapper<const value_type> &wrapper, context_type &context)
  {
    return formatter<value_type, char_type>::format(wrapper.get(), context);
  }
};

template<typename char_type_>
struct fmt::formatter<std::thread::id, char_type_> : fmt::formatter<const char *, char_type_>
{
  template<typename context_type>
  auto format(const std::thread::id &id, context_type &context) requires std::is_same_v<std::thread::native_handle_type, pthread_t>
  {
    std::array<char, 16> thread_name {};
    ::pthread_getname_np(*reinterpret_cast<const pthread_t *>(&id), thread_name.data(), 16);
    return format_to(context.out(), /*FMT_COMPILE*/ ("{}({})"), id, thread_name.data());
  }
};

template<typename char_type_>
struct fmt::formatter<::timespec, char_type_> : fmt::formatter<const char *, char_type_>
{
  template<typename context_type>
  auto format(const ::timespec &ts, context_type &context)
  {
    return format_to(context.out(), /*FMT_COMPILE*/ ("{:%T}.{:09d}"), fmt::gmtime(ts.tv_sec), ts.tv_nsec);
  }
};

template<>
struct fmt::formatter<std::wstring> : fmt::formatter<const char *>
{
  template<typename context_type>
  auto format(const std::wstring &string, context_type &context)
  {
    return std::transform(string.begin(), string.end(), context.out(), [](auto c) { return char(c); });
  }
};

template<typename char_type>
struct fmt::formatter<std::filesystem::path, char_type> : fmt::formatter<const char *, char_type>
{
  template<typename context_type>
  auto format(const std::filesystem::path &path, context_type &context)
  {
    std::filesystem::path::string_type as_string(path);
    return std::transform(as_string.begin(), as_string.end(), context.out(), [](auto c) { return char_type(c); });
  }
};

template<typename variant_type, typename char_type>
struct fmt::formatter<variant_type, char_type, std::variant_alternative_t<0, variant_type>> : fmt::dynamic_formatter<char_type>
{
  template<typename format_context_type>
  auto format(const variant_type &variant, format_context_type &ctx)
  {
    return std::visit([&, this](const auto &value) { return dynamic_formatter<char_type>::format(value, ctx); }, variant);
  }
};

template<typename value_type, typename char_type_>
struct default_formatter : fmt::formatter<const char *, char_type_>
{
  template<typename context_type>
  auto format(const value_type &value, context_type &context)
  {
    return fmt::format_to(context.out(), /*FMT_COMPILE*/ ("{} at {0x%016x}"), boost::typeindex::type_id<value_type>().pretty_name(), static_cast<const void*>(std::addressof(value)));
  }
};

