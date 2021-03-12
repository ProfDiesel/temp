#pragma once

#include "../feed/feed.hpp"
#include "config_reader.hpp"
#include "properties_dispatch.hpp"

#include <boilerplate/boilerplate.hpp>

#include <chrono>
#include <type_traits>
#include <variant>

#if !defined(__clang__)
#  include <decimal/decimal>
#endif // !defined(__clang__)

namespace config
{
template<>
struct from_walker_impl<std::chrono::nanoseconds>
{
  auto operator()(const walker &w) { return std::chrono::nanoseconds(static_cast<std::int64_t>(*w)); }
};

struct address
{
  std::string host, port;
};

template<>
struct from_walker_impl<address>
{
  auto operator()(const walker &w)
  {
    const config::string_type &as_string = *w;
    const auto n = as_string.find(':');
    return address {as_string.substr(0, n), as_string.substr(n + 1)};
  }
};

template<>
struct from_walker_impl<feed::price_t>
{
#if !defined(__clang__)
  auto operator()(const walker &w) { return feed::price_t(std::decimal::decimal32(static_cast<config::numeric_type>(*w))); }
#else  // !defined(__clang__)
  auto operator()(const walker &w) { return feed::price_t(static_cast<config::numeric_type>(*w)); }
#endif // !defined(__clang__)
};

inline auto dispatch_hash(const walker &w)
{
  const config::string_type &as_string = *w;
  return dispatch::hash {}(as_string);
}

} // namespace config
