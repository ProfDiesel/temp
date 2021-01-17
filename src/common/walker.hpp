#pragma once

#include "../feed/feed.hpp"
#include "config_reader.hpp"
#include "properties_dispatch.hpp"

#include <chrono>
#include <type_traits>
#include <variant>

#if !defined(__clang__)
#  include <decimal/decimal>
#endif // !defined(__clang__)

template<typename...>
constexpr bool always_false = false;

namespace config
{
template<typename value_type>
struct from_walker_impl;

namespace detail
{
template<typename value_type>
auto has_from_walker_v(int) -> decltype(std::declval<from_walker_impl<value_type>>()(std::declval<properties::walker>()), std::true_type {});

template<typename value_type>
std::false_type has_from_walker_v(char);
} // namespace detail

template<typename value_type>
constexpr bool has_from_walker_v = decltype(detail::has_from_walker_v<value_type>(0))::value;

struct from_walker_t final
{
  const properties::walker &w;

  template<typename value_type>
  operator value_type() const &
  {
    static_assert(always_false<value_type>);
  }

  template<typename value_type>
  operator value_type() const &&
  {
    static_assert(has_from_walker_v<value_type>);
    return from_walker_impl<value_type> {}(w);
  }
};
inline from_walker_t from_walker(const properties::walker &w) { return from_walker_t {w}; }

template<>
struct from_walker_impl<clock::duration>
{
  auto operator()(const properties::walker &w) { return clock::duration((std::int64_t)std::get<config::numeric_type>(w.get())); }
};

struct address
{
  std::string host, port;
};

template<>
struct from_walker_impl<address>
{
  auto operator()(const properties::walker &w)
  {
    const config::string_type &as_string = w;
    const auto n = as_string.find(':');
    return address {as_string.substr(0, n), as_string.substr(n + 1)};
  }
};

template<>
struct from_walker_impl<feed::price_t>
{
#if !defined(__clang__)
  auto operator()(const properties::walker &w) { return feed::price_t(std::decimal::decimal32(std::get<config::numeric_type>(w.get()))); }
#else // !defined(__clang__)
  auto operator()(const properties::walker &w) { return feed::price_t(std::get<config::numeric_type>(w.get())); }
#endif // !defined(__clang__)
};

inline auto dispatch_hash(const properties::walker &w)
{
  const config::string_type &as_string = w;
  return dispatch::hash {}(as_string);
}

} // namespace config
