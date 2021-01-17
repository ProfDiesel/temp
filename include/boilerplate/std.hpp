#pragma once

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
static_assert(std::atomic<atomic_unsigned_lock_free>::is_always_lock_free);
#endif // !defined(__clang__)

#if defined(__clang__)
template<typename iterator_type>
constexpr auto contiguous_iterator = __is_cpp17_contiguous_iterator<iterator_type>::value;
#endif // defined(__clang__)

} // namespace std

