#pragma once

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <tuple>
#include <type_traits>

namespace boilerplate
{
static constexpr auto required_bits(uint64_t x) { return std::numeric_limits<uint64_t>::digits - __builtin_clzll(x - 1); }

static constexpr auto next_power_of_2(auto x) { return std::make_unsigned_t<decltype(x)> {1} << required_bits(x); }

template<typename, typename>
struct tuple_contains_type;

template<typename requested_type, typename... tuple_element_types>
struct tuple_contains_type<requested_type, std::tuple<tuple_element_types...>> : std::disjunction<std::is_same<requested_type, tuple_element_types>...>
{
};

template<typename requested_type, typename tuple_type>
inline constexpr bool tuple_contains_type_v = tuple_contains_type<requested_type, tuple_type>::value;

template<typename>
struct is_tuple : std::false_type
{
};

template<typename... tuple_element_types>
struct is_tuple<std::tuple<tuple_element_types...>> : std::true_type
{
};

template<typename value_type>
inline constexpr bool is_tuple_v = is_tuple<value_type>::value;


template<class... Ts>
struct overloaded : Ts...
{
  using Ts::operator()...;
};

template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>; // not needed as of C++21

template<class T>
struct make
{
};

template<typename value_type>
auto &const_cast_(value_type &value)
{
  return const_cast<std::remove_const_t<value_type> &>(value);
}

struct empty final
{
};

constexpr void do_nothing() noexcept {}

// Used to forbid template instantiation. ``static_assert(always_false<whatever>);``
template<typename...>
constexpr bool always_false = false;

} // namespace boilerplate
