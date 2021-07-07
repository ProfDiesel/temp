#pragma once

#include <gsl/pointers>
#if defined(__clang__)
#  include <nonstd/observer_ptr.hpp>
#else
#  include <experimental/memory>
#endif // !defined(__clang__)

namespace boilerplate
{
#if defined(__clang__)
using nonstd::make_observer;
using nonstd::observer_ptr;
#else
using std::experimental::make_observer;
using std::experimental::observer_ptr;
#endif // !defined(__clang__)

template<typename value_type>
using maybe_null_observer_ptr = observer_ptr<value_type>;

template<typename value_type>
using not_null_observer_ptr = gsl::strict_not_null<observer_ptr<value_type>>;

template<class T>
auto make_strict_not_null(T &&t) noexcept
{
  return gsl::make_strict_not_null(make_observer(std::forward<T>(t)));
}

} // namespace boilerplate
