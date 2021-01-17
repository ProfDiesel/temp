#pragma once

#include <boost/hana/first.hpp>
#include <boost/hana/fold_left.hpp>
#include <boost/hana/insert.hpp>
#include <boost/hana/integral_constant.hpp>
#include <boost/hana/intersection.hpp>
#include <boost/hana/map.hpp>
#include <boost/hana/negate.hpp>
#include <boost/hana/pair.hpp>
#include <boost/hana/second.hpp>
#include <boost/hana/symmetric_difference.hpp>

#include <boost/operators.hpp>

namespace units
{

namespace b = boost;
namespace h = boost::hana;

using namespace h::literals;

namespace detail
{
const auto is_scalar = [](auto map) { return h::length(map) == 0; };

const auto product = [](auto map0, auto map1) {
  return h::fold_left(h::intersection(map0, map1), h::symmetric_difference(map0, map1), [map1](auto map, auto pair) {
    constexpr auto key = h::first(pair);
    constexpr auto value = h::second(pair) + h::find(map1, key).value();
    if constexpr(h::equal(value, h::int_c<0>))
      return map;
    return h::insert(map, h::make_pair(key, value));
  });
};

const auto invert = [](auto map) {
  return h::fold_left(map, h::make_map(), [](auto map, auto pair) { return h::insert(map, h::make_pair(h::first(pair), -h::second(pair))); });
};

} // namespace detail

template<typename dimension_map_type, typename unit_type>
struct quantity : public b::additive<quantity<dimension_map_type, unit_type>>, public b::multiplicative<quantity<dimension_map_type, unit_type>, unit_type>, b::equivalent<quantity<dimension_map_type, unit_type>>, b::less_than_comparable<quantity<dimension_map_type, unit_type>>
{
  static_assert(!std::is_scalar_v<dimension_map_type>);
  //  static_assert(std::is_arithmetic_v<unit_type>);

  //  static_assert(sizeof(quantity<dimension_map_type, unit_type>) == sizeof(unit_type));
  //  static_assert(std::is_trivial_v<quantity<dimension_map_type, unit_type>>);

  using self_type = quantity<dimension_map_type, unit_type>;

  template<typename other_unit_type>
  using with_type = quantity<dimension_map_type, other_unit_type>;

  constexpr quantity() = default;
  constexpr explicit quantity(const unit_type &value): value(value) {}

  constexpr const auto &get() const { return value; }
  constexpr explicit operator const unit_type &() const { return value; }

  constexpr bool operator<(const self_type &other) const { return value < other.value; }

  constexpr self_type &operator+=(const self_type &other) { value += other.value; return *this; }
  constexpr self_type &operator-=(const self_type &other) { value -= other.value; return *this; }

  template<typename scalar_type>
  constexpr self_type &operator*=(const scalar_type &value) { this->value *= value; return *this; }
  template<typename scalar_type>
  constexpr self_type &operator/=(const scalar_type &value) { this->value /= value; return *this; }

  template<typename other_dimension_map_type, typename other_unit_type>
  constexpr auto operator*(const quantity<other_dimension_map_type, other_unit_type> &other) const
  {
    constexpr auto result_dimension_map = detail::product(std::declval<dimension_map_type>(), std::declval<other_dimension_map_type>());
    const auto value = this->value * other.value;
    if constexpr(detail::is_scalar(result_dimension_map))
      return value;
    return quantity<decltype(result_dimension_map), decltype(value)> {value};
  }

  template<typename other_dimension_map_type, typename other_unit_type>
  constexpr auto operator/(const quantity<other_dimension_map_type, other_unit_type> &other) const
  {
    constexpr auto result_dimension_map = detail::product(std::declval<dimension_map_type>(), detail::invert(std::declval<other_dimension_map_type>()));
    const auto value = this->value / other.value;
    if constexpr(detail::is_scalar(result_dimension_map))
      return value;
    return quantity<decltype(result_dimension_map), decltype(value)> {value};
  }

  template<std::intmax_t numerator, std::intmax_t denominator>
  struct ratio
  {
    static constexpr std::intmax_t num = numerator, den = denominator;
    using value_type = ratio<numerator, denominator>;
    static constexpr value_type value {};

    constexpr operator quantity<dimension_map_type, unit_type>() const { return {num / den}; }
  };

  template<std::intmax_t value>
  struct integral_constant
  {
    constexpr operator quantity<dimension_map_type, unit_type>() const { return {value}; }
  };

private:
  unit_type value {};
};

template<typename dimension_type>
static constexpr auto simple_dimension_map = h::make_map(h::make_pair(h::type_c<dimension_type>, 1_c));

template<typename dimension_type, typename unit_type>
using make_quantity_type = quantity<decltype(simple_dimension_map<dimension_type>), unit_type>;

} // namespace units
