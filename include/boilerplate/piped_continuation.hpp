#pragma once

#include <boilerplate/boilerplate.hpp>

#include <boost/hof/lazy.hpp>
#include <boost/hof/lift.hpp>
#include <boost/hof/partial.hpp>

namespace piped_continuation
{
template<typename function_type, typename continuation_type>
[[using gnu : always_inline, flatten]] auto operator|=(function_type &&function, continuation_type &&continuation)
{
  // return hof::lazy(hof::partial(hof::lazy(function))(continuation));
  return [=](auto &&... args) { return function(continuation, std::forward<decltype(args)>(args)...); };
}

template<typename function_type, typename continuation_type>
[[using gnu : always_inline, flatten]] auto operator%=(function_type &&function, continuation_type &&continuation)
{
  return [=](auto &&...args) { return continuation(function(std::forward<decltype(args)>(args)...)); };
}

} // namespace piped_continuation

