#pragma once

#include <string_view>
#include <type_traits>
#include <tuple>
#include <variant>


namespace boilerplate
{

//
// library fundamentals V2

namespace detail
{

template<typename default_type, typename _, template<typename...> class what_type, typename... args_types>
struct detector
{
	using value_t = std::false_type;
	using type = default_type;
};
 
template<typename default_type, template<typename...> class what_type, typename... args_types>
struct detector<default_type, std::void_t<what_type<args_types...>>, what_type, args_types...>
{
	using value_t = std::true_type;
	using type = what_type<args_types...>;
};
 
} // namespace detail

struct nonesuch
{
    nonesuch() = delete;
    ~nonesuch() = delete;
    nonesuch(nonesuch const&) = delete;
    void operator=(nonesuch const&) = delete;
};

template<template<typename...> class what_type, typename... args_types>
using is_detected = typename detail::detector<nonesuch, void, what_type, args_types...>::value_t;
 
template<template<typename...> class what_type, typename... args_types>
using detected_t = typename detail::detector<nonesuch, void, what_type, args_types...>::type;
 
template<template<typename...> class what_type, typename... args_types>
constexpr bool is_detected_v = is_detected<what_type, args_types...>::value;

template<typename default_type, template<typename...> class what_type, typename... args_types>
using detected_or = detail::detector<default_type, void, what_type, args_types...>;

template<typename default_type, template<typename...> class what_type, typename... args_types>
using detected_or_t = typename detected_or<default_type, what_type, args_types...>::type;

template<typename Expected, template<typename...> class what_type, typename... args_types>
using is_detected_exact = std::is_same<Expected, detected_t<what_type, args_types...>>;

template<typename Expected, template<typename...> class what_type, typename... args_types>
constexpr bool is_detected_exact_v = is_detected_exact<Expected, what_type, args_types...>::value;

template<typename to_type, template<typename...> class what_type, typename... args_types>
using is_detected_convertible = std::is_convertible<detected_t<what_type, args_types...>, to_type>;

template<typename to_type, template<typename...> class what_type, typename... args_types>
constexpr bool is_detected_convertible_v = is_detected_convertible<to_type, what_type, args_types...>::value;

//
//

template<typename value_type, typename = void>
struct is_iterable_t : std::false_type { };
template<typename value_type>
struct is_iterable_t<value_type, std::void_t<decltype(std::declval<value_type>().begin()), decltype(std::declval<value_type>().end())>> : std::true_type { };
template<typename value_type>
inline constexpr auto is_iterable_v = is_iterable_t<value_type>::value;

template<typename value_type, template<typename...> class element_predicate_t>
struct is_iterable_of
{
	static constexpr auto value_()
	{
		if constexpr(is_iterable_v<value_type>) { if constexpr(element_predicate_t<typename value_type::value_type>::value) return true; }
		return false;
	}

	static constexpr auto value = value_();
};

template<typename value_type, template<typename...> class element_predicate_t>
constexpr bool is_iterable_of_v = is_iterable_of<value_type, element_predicate_t>::value;

template<typename value_type>
using is_string = std::is_convertible<value_type, std::string_view>;

template<typename value_type>
constexpr bool is_string_v = std::is_convertible_v<value_type, std::string_view>;

template<typename value_type>
using first_variant_alternative_t = std::variant_alternative_t<0, value_type>;

template<typename value_type>
using is_variant_t = is_detected<first_variant_alternative_t, value_type>;

template<typename value_type>
constexpr bool is_variant_v = is_detected_v<first_variant_alternative_t, value_type>;

template<typename value_type>
using first_tuple_element_t = std::tuple_element_t<0, value_type>;

template<typename value_type>
using is_tuple_t = is_detected<first_tuple_element_t, value_type>;

template<typename value_type>
constexpr bool is_tuple_v = is_detected_v<first_tuple_element_t, value_type>;

} // namespace boilerplate

#undef __LIGNE_R__BOILERPLATE__TYPE_PREDICATE__
