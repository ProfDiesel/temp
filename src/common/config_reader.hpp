#pragma once

#include <boilerplate/boilerplate.hpp>
#include <boilerplate/chrono.hpp>
#include <boilerplate/contracts.hpp>
#include <boilerplate/leaf.hpp>
#include <boilerplate/logger.hpp>
#include <boilerplate/pointers.hpp>
#include <boilerplate/std.hpp>
#include <boilerplate/x3.hpp>

#include <boost/fusion/include/std_tuple.hpp>

#include <boost/range/iterator_range.hpp>
#include <boost/spirit/include/support_istream_iterator.hpp>

#include <fmt/compile.h>
#include <fmt/format.h>

#include <frozen/string.h>

#if defined(DEBUG)
#  include <unordered_map>
#else
#  include <robin_hood.h>
#endif // defined(DEBUG)

#include <functional>
#include <istream>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

#if !defined(DEBUG)
template<>
struct robin_hood::hash<frozen::string>
{
  size_t operator()(const frozen::string &s) const noexcept { return hash_bytes(s.data(), s.size()); }
};
#endif // !defined(DEBUG)

namespace config
{
// TODO: pre-hash it
using hashed_string = frozen::string;

namespace literals
{
// template<typename char_type, char_type... c>
// constexpr auto operator"" _hs()
// {
//   static constexpr char_type data[] = {c..., '\0'};
//   return frozen::string(data);
// }

constexpr hashed_string operator"" _hs(const char *data, std::size_t size) { return {data, size}; }

} // namespace literals

using string_type = std::string;
using numeric_type = double;
using string_list_type = std::vector<string_type>;
using numeric_list_type = std::vector<numeric_type>;
using value_type = std::variant<std::monostate, string_type, numeric_type, string_list_type, numeric_list_type>;

struct parse_error
{
  std::pair<std::size_t, std::size_t> indices;
  std::string which;
  std::string snippet;
};

template<typename continuation_type, typename iterator_type>
static boost::leaf::result<bool> parse(const continuation_type &continuation, iterator_type first, iterator_type last)
{
  namespace x3 = boost::spirit::x3;
  using namespace x3_ext;

  using name_type = std::conditional_t<std::contiguous_iterator<iterator_type>, boost::iterator_range<iterator_type>, std::string>;
  using assignment_type = std::tuple<name_type, name_type, value_type>;

  static const auto comment = x3::rule<class comment>("comment") = '#' >> *(x3::char_ - x3::eol) >> x3::eol;
  static const auto discard = x3::space | comment;

  static const auto quoted_string = x3::rule<class quoted_string_class, string_type>("quoted string")
    = x3::lexeme['\'' >> x3::raw[*(x3::char_ - '\'')] >> expect('\'')];
  static const auto string_list = x3::rule<class string_list_class, string_list_type>("string list") = '[' >> (quoted_string % ',') >> expect(']');
  static const auto numeric_list = x3::rule<class numeric_list_class, numeric_list_type>("numeric list") = '[' >> -(x3::double_ % ',') >> expect(']');
  static const auto value = x3::rule<class value_class, value_type>("value") = quoted_string | x3::double_ | string_list | numeric_list;
  static const auto object_name = x3::rule<class object_name_class, name_type>("object") = x3::raw[(x3::alpha | '_') >> *(x3::alnum | '_')];
  static const auto field_name = x3::rule<class field_name_class, name_type>("field") = x3::raw[(x3::alpha | '_') >> *(x3::alnum | '_')];
  static const auto assignment = x3::rule<class assignment_class, assignment_type>("assignment")
    = x3::lexeme[-(object_name >> ".") >> expect(field_name)] >> expect("<-") >> expect(value) >> expect(";");

  const auto grammar = *(!x3::eoi >> assignment[(
                           [&](auto &context)
                           {
                             auto &[object, field, value] = x3::_attr(context);
                             continuation(object, field, std::move(value));
                           })]);

  return boost::leaf::try_handle_some(
    [&]()
    {
      boost::leaf::error_monitor monitor;
      auto it = first;
      return (x3::phrase_parse(it, last, grammar, discard) && (it == last)) ? boost::leaf::result<bool> {true} : monitor.check();
    },
    [&](const expectation_failure<iterator_type> &error /*, const boost::leaf::e_source_location &location*/)
    {
      std::string snippet;
      const auto where = error.where.begin();
      std::copy_n(where, std::min(10, static_cast<int>(std::distance(where, last))), std::back_inserter(snippet));
      const std::pair indices {static_cast<std::size_t>(std::distance(first, error.where.begin())),
                               static_cast<std::size_t>(std::distance(first, error.where.end()))};

      return BOOST_LEAF_NEW_ERROR(parse_error {.indices = indices, .which = error.which, .snippet = snippet});
    });
}

template<typename value_type>
using char_t = typename value_type::char_type;

template<typename value_type>
using traits_t = typename value_type::traits_type;

auto parse(auto &&continuation, auto &&from)
{
  using from_type_ = std::decay_t<decltype(from)>;
  using char_type = std::detected_or_t<char, char_t, from_type_>;
  using traits_type = std::detected_or_t<std::char_traits<char_type>, traits_t, from_type_>;
  if constexpr(std::is_base_of_v<std::basic_istream<char_type, traits_type>, from_type_>)
  {
    boost::spirit::basic_istream_iterator<char_type, traits_type> begin {std::forward<decltype(from)>(from)}, end;
    return parse(std::forward<decltype(continuation)>(continuation), begin, end);
  }
  else
    return parse(std::forward<decltype(continuation)>(continuation), from.begin(), from.end());
}

namespace detail
{
using object_name_type = std::string;
using field_name_type = std::string;

static constexpr auto tolower = [](char c) constexpr noexcept { return ((c >= 'A') && (c <= 'Z')) ? c + 'a' - 'A' : c; };
static constexpr auto equals_to = [](const auto &s0, const auto &s1) constexpr noexcept
{
  return std::equal(s0.begin(), s0.end(), s1.begin(), s1.end(), [](char c0, char c1) { return tolower(c0) == tolower(c1); });
};
static constexpr auto hash_combine = [](std::size_t h0, std::size_t h1, std::size_t prime = 0x00000100000001B3) constexpr noexcept
{
  return (h0 ^ h1) * prime;
}; // boost::hash_combine is not constexpr
static constexpr auto hash = [](const auto &s, std::size_t seed = 0xcbf29ce484222325) constexpr noexcept
{
  for(auto &&c: s)
    hash_combine(seed, std::size_t(tolower(c)));
  return seed;
};
#if defined(DEBUG)
template<typename key_type, typename mapped_type>
using map_type = std::unordered_map<key_type, mapped_type, decltype(hash), decltype(equals_to)>;
#else  // defined(_DEBUG)
template<typename key_type, typename mapped_type>
using map_type = robin_hood::unordered_flat_map<key_type, mapped_type, decltype(hash), decltype(equals_to)>;
#endif // defined(_DEBUG)

using object_type = map_type<field_name_type, value_type>;
using world_type = map_type<object_name_type, object_type>;

} // namespace detail

template<typename value_type>
struct from_walker_impl;

struct walker;

namespace detail
{
template<typename value_type>
auto has_from_walker_v(int) -> decltype(std::declval<from_walker_impl<value_type>>()(std::declval<walker>()), std::true_type {});

template<typename value_type>
std::false_type has_from_walker_v(char);
} // namespace detail

template<typename value_type>
constexpr bool has_from_walker_v = decltype(detail::has_from_walker_v<value_type>(0))::value;

struct from_walker_t final
{
  const walker &w;

  template<typename value_type>
  operator value_type() const &noexcept
  {
    static_assert(boilerplate::always_false<value_type>);
  }

  template<typename value_type>
  operator value_type() const &&noexcept
  {
    static_assert(has_from_walker_v<value_type>);
    return from_walker_impl<value_type> {}(w);
  }

  template<typename value_type>
  bool operator==(value_type &&value) const &&noexcept
  {
    static_assert(has_from_walker_v<value_type>);
    return from_walker_impl<value_type> {}(w) == std::forward<value_type>(value);
  }
};
inline from_walker_t from_walker(const walker &w) noexcept { return from_walker_t {w}; }

struct walker final
{
  auto has_value() const noexcept { return LIKELY(!std::holds_alternative<std::monostate>(value)); }

  auto get() const noexcept
  {
    ASSERTS(*this);
    return from_walker(*this);
  }

  auto get_or(auto &&value) const noexcept { return has_value() ? static_cast<decltype(value)>(get()) : std::forward<decltype(value)>(value); }

  walker get(const std::size_t &index) const noexcept
  {
    if(std::holds_alternative<string_list_type>(value))
      return {data, std::get<string_list_type>(value)[index]};
    else if(std::holds_alternative<numeric_list_type>(value))
      return {data, std::get<numeric_list_type>(value)[index]};
    else [[unlikely]]
      return {data};
  }

  walker get(const detail::field_name_type &index) const noexcept
  {
    if(!resolve()) [[unlikely]]
      return {data};
    else if(auto it = object->find(index); it != object->end()) [[likely]]
      return {data, it->second};
    else [[unlikely]]
      return {data};
  }

  // TODO
  walker get(const hashed_string &index) const noexcept { return get(detail::field_name_type(index.data())); }

  bool resolve() const noexcept
  {
    if(object)
      return true;
    if(!std::holds_alternative<string_type>(value)) [[unlikely]]
      return false;
    if(auto it = data->find(detail::object_name_type(std::get<string_type>(value))); it != data->end()) [[likely]]
      object.reset(&it->second);
    return LIKELY(!!object);
  }

  operator bool() const noexcept { return has_value(); }
  auto operator*() const noexcept { return get(); }
  walker operator[](auto index) const noexcept { return get(index); }

  gsl::strict_not_null<std::shared_ptr<const detail::world_type>> data;
  value_type value {};
  mutable boilerplate::maybe_null_observer_ptr<const detail::object_type> object {};
};

template<>
struct from_walker_impl<value_type>
{
  auto operator()(const walker &w) noexcept { return w.get(); }
};

template<>
struct from_walker_impl<bool>
{
  auto operator()(const walker &w)
  {
    return std::visit(
      boilerplate::overloaded {
        [&](const string_type &alternative) { return alternative == "true" || w.resolve(); },
        [&](const numeric_type &alternative) { return bool(alternative); },
        [&](const string_list_type &alternative) { return !alternative.empty(); },
        [&](const numeric_list_type &alternative) { return !alternative.empty(); },
        [&](const auto &) { return false; },
      },
      w.value);
  }
};

template<>
struct from_walker_impl<string_type>
{
  auto operator()(const walker &w)
  {
    static const string_type default_result;
    return std::holds_alternative<string_type>(w.value) ? std::get<string_type>(w.value) : default_result;
  }
};

template<typename value_type>
requires std::is_arithmetic_v<value_type>
struct from_walker_impl<value_type>
{
  auto operator()(const walker &w)
  {
    return std::holds_alternative<numeric_type>(w.value) ? static_cast<value_type>(std::get<numeric_type>(w.value)) : value_type {};
  }
};

template<>
struct from_walker_impl<string_list_type>
{
  auto operator()(const walker &w)
  {
    static const string_list_type default_result;
    return std::holds_alternative<string_list_type>(w.value) ? std::get<string_list_type>(w.value) : default_result;
  }
};

template<>
struct from_walker_impl<numeric_list_type>
{
  auto operator()(const walker &w)
  {
    static const numeric_list_type default_result;
    return std::holds_alternative<numeric_list_type>(w.value) ? std::get<numeric_list_type>(w.value) : default_result;
  }
};

struct properties final
{
  template<typename input_type>
  static boost::leaf::result<properties> create(input_type &&input) noexcept
  {
    auto data = std::make_shared<detail::world_type>();
    BOOST_LEAF_CHECK(parse(
      [&](auto object, auto field, auto &&value)
      {
        auto [it, _] = data->try_emplace(detail::object_name_type {object.begin(), object.end()}, detail::object_type {});
        it->second.insert_or_assign(detail::field_name_type {field.begin(), field.end()}, std::forward<decltype(value)>(value));
      },
      std::forward<input_type>(input)));
    return {properties {std::move(data)}};
  }

  walker get(const detail::object_name_type &index) const noexcept
  {
    if(auto it = data->find(index); it != data->end())
      return {gsl::make_not_null(data), it->first, boilerplate::make_strict_not_null(&it->second)};
    return {gsl::make_not_null(data)};
  }

  walker get(const hashed_string &index) const noexcept
  {
    return (*this)[detail::object_name_type(index.data())]; // TODO
  }

  walker get(const std::tuple<detail::object_name_type, detail::field_name_type> &index) const noexcept
  {
    const auto &[object, field] = index;
    return (*this)[object][field];
  }

  walker operator[](auto index) const noexcept { return get(index); }

  std::shared_ptr<const detail::world_type> data {};
};

} // namespace config

#if defined(DOCTEST_LIBRARY_INCLUDED)
// GCOVR_EXCL_START
#  include <string_view>

TEST_SUITE("config_reader")
{
  using namespace config::literals;
  using namespace std::string_literals;
  using namespace std::string_view_literals;

  TEST_CASE("parser")
  {
    SUBCASE("success")
    {
      const auto config_str = "\
a.a <- 'string';\n\
a.b <- 42;\n\
a.c <- ['string', 'list'];\n\
a.d <- [0, 1, 2, 3, 4];\n\n"sv;
      boost::leaf::try_handle_all(
        [&]() -> boost::leaf::result<void>
        {
          const auto result = BOOST_LEAF_TRYX(config::properties::create(config_str));
          const auto a = result["a"_hs];
          CHECK(a);
          CHECK(*a["a"_hs] == "string"s);
          CHECK(*a["b"_hs] == 44.0);
          CHECK(*a["c"_hs] == std::vector {"string"s, "list"s});
          CHECK(*a["d"_hs] == std::vector {0.0, 1.0, 2.0, 3.0, 4.0});
          return {};
        },
        [&]([[maybe_unused]] const boost::leaf::error_info &unmatched) { CHECK(false); });
    }
    SUBCASE("failure")
    {
      const auto config_str = "\
parsing.fails <- here;\n\n"sv;
      bool success = false;
      boost::leaf::try_handle_all(
        [&]() -> boost::leaf::result<void>
        {
          BOOST_LEAF_CHECK(config::properties::create(config_str));
          return {};
        },
        [&](const config::parse_error &error)
        {
          CHECK(error.indices == std::pair {std::size_t {17}, std::size_t {24}});
          CHECK(error.which == "value");
          success = true;
        },
        [&]([[maybe_unused]] const boost::leaf::error_info &unmatched) { CHECK(false); });
      CHECK(success);
    }
    SUBCASE("walker")
    {
      const auto config_str = "\
a.next <- 'b';\n\
a.value <- 1;\n\
b.next <- 'c';\n\
b.value <- 'string';\n\
c.next <- ['a', 'b'];\n\n"sv;
      boost::leaf::try_handle_all(
        [&]() -> boost::leaf::result<void>
        {
          const auto result = BOOST_LEAF_TRYX(config::properties::create(config_str));
          const auto a = result["a"_hs];
          CHECK(a);
          CHECK(*a == "a"s);
          CHECK(*a["value"_hs] == 1.0);
          const auto b = a["next"_hs];
          CHECK(b);
          CHECK(*b == "b"s);
          CHECK(*b["value"_hs] == "string"s);
          const auto c = b["next"_hs];
          CHECK(c);
          CHECK(*c == "c"s);
          const auto a_ = c["next"_hs][0];
          const auto b_ = c["next"_hs][1];
          CHECK(a_);
          CHECK(b_);
          CHECK(*b_ == "b"s);
          CHECK(*b_["next"_hs] == "c"s);

          return {};
        },
        [&]([[maybe_unused]] const boost::leaf::error_info &unmatched) { CHECK(false); });
    }
  }
}
// GCOVR_EXCL_STOP
#endif // defined(DOCTEST_LIBRARY_INCLUDED)
