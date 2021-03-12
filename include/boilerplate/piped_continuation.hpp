#pragma once

#include <boilerplate/boilerplate.hpp>

namespace piped_continuation
{
template<typename function_type, typename continuation_type>
[[using gnu : always_inline, flatten]] auto operator|=(const function_type &function, const continuation_type &continuation)
{
  return [=](auto &&... args) { return function(continuation, std::forward<decltype(args)>(args)...); };
}

template<typename function_type, typename continuation_type>
[[using gnu : always_inline, flatten]] auto operator|=(function_type &&function, continuation_type &&continuation)
{
  return [=](auto &&... args) mutable { return function(continuation, std::forward<decltype(args)>(args)...); };
}

template<typename function_type, typename continuation_type>
[[using gnu : always_inline, flatten]] auto operator%=(const function_type &function, const continuation_type &continuation)
{
  return [=](auto &&...args) { return continuation(function(std::forward<decltype(args)>(args)...)); };
}

template<typename function_type, typename continuation_type>
[[using gnu : always_inline, flatten]] auto operator%=(function_type &&function, continuation_type &&continuation)
{
  return [=](auto &&...args) mutable { return continuation(function(std::forward<decltype(args)>(args)...)); };
}

} // namespace piped_continuation

