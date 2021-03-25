#pragma once

#include <experimental/type_traits>

namespace std
{
  constexpr auto hardware_destructive_interference_size = 64;

  /*
template<class T, class... Args>
constexpr T *construct_at(T *p, Args &&... args)
{
  return ::new(const_cast<void *>(static_cast<const volatile void *>(p))) T(std::forward<Args>(args)...);
}
*/

#if !defined(__clang__)
  using atomic_unsigned_lock_free = atomic_uint_fast64_t;
  static_assert(atomic_unsigned_lock_free::is_always_lock_free);
#endif // !defined(__clang__)

#if defined(__clang__)
  template <typename iterator_type>
  constexpr auto contiguous_iterator = __is_cpp17_contiguous_iterator<iterator_type>::value;
#endif // defined(__clang__)

  using experimental::detected_or_t;

#if defined(__clang__)
namespace chrono
{
  template<typename>
  using is_clock = std::true_type;

  template<typename value_type>
  constexpr auto is_clock_v = is_clock<value_type>::value;
} // namespace chrono
#endif // defined(__clang__)
} // namespace std
