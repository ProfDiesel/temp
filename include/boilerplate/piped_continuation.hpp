#pragma once

#include <type_traits>
#include <utility>

namespace piped_continuation
{

template<typename function_type, typename continuation_type>
[[using gnu: always_inline, flatten]] inline auto operator|=(function_type &&function, continuation_type &&continuation)
{
  if constexpr(std::is_const_v<decltype(function)> && std::is_const_v<decltype(continuation)>)
    return [=](auto &&...args) {
      static_assert(std::is_invocable_v<decltype(function), decltype(continuation), decltype(args)...>);
      return std::forward<decltype(function)>(function)(std::forward<decltype(continuation)>(continuation), std::forward<decltype(args)>(args)...);
    };
  else
    return [=](auto &&...args) mutable {
      static_assert(std::is_invocable_v<decltype(function), decltype(continuation), decltype(args)...>);
      return std::forward<decltype(function)>(function)(std::forward<decltype(continuation)>(continuation), std::forward<decltype(args)>(args)...);
    };
}

template<typename function_type, typename continuation_type>
[[using gnu: always_inline, flatten]] inline auto operator%=(function_type &&function, continuation_type &&continuation)
{
  if constexpr(std::is_const_v<decltype(function)> && std::is_const_v<decltype(continuation)>)
    return [=](auto &&...args) {
      static_assert(std::is_invocable_v<decltype(continuation), decltype(function), decltype(args)...>);
      return std::forward<decltype(continuation)>(continuation)(std::forward<decltype(function)>(function)(std::forward<decltype(args)>(args)...));
    };
  else
    return [=](auto &&...args) mutable {
      static_assert(std::is_invocable_v<decltype(continuation), decltype(function), decltype(args)...>);
      return std::forward<decltype(continuation)>(continuation)(std::forward<decltype(function)>(function)(std::forward<decltype(args)>(args)...));
    };
}

} // namespace piped_continuation
