#pragma once

#include <boilerplate/boilerplate.hpp>
#include <boilerplate/chrono.hpp>
#include <boilerplate/leaf.hpp>
#include <boilerplate/logger.hpp>
#include <boilerplate/pointers.hpp>
#include <boilerplate/std.hpp>
#include <boilerplate/type_traits.hpp>
#include <boilerplate/x3.hpp>

#include <boost/fusion/include/std_tuple.hpp>

#include <boost/range/iterator_range.hpp>
#include <boost/spirit/include/support_istream_iterator.hpp>

#include <fmt/compile.h>
#include <fmt/format.h>

#include <frozen/string.h>

#if defined(_DEBUG)
#  include <unordered_map>
#else
#  include <robin_hood.h>
#endif // defined(_DEBUG)

#include <functional>
#include <istream>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

namespace leaf = boost::leaf;
namespace b = boilerplate;

template<>
struct robin_hood::hash<frozen::string>
{
  size_t operator()(const frozen::string &s) const noexcept { return hash_bytes(s.data(), s.size()); }
};

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
static leaf::result<bool> parse(const continuation_type &continuation, iterator_type first, iterator_type last)
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

  const auto grammar = *(!x3::eoi >> assignment[([&](auto &context) {
    auto &[object, field, value] = x3::_attr(context);
    continuation(object, field, std::move(value));
  })]);

  return leaf::try_handle_some(
    [&]() {
      leaf::error_monitor monitor;
      auto it = first;
      return (x3::phrase_parse(it, last, grammar, discard) && (it == last)) ? leaf::result<bool> {true} : monitor.check();
    },
    [&](const expectation_failure<iterator_type> &error /*, const leaf::e_source_location &location*/) {
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

template<typename continuation_type, typename from_type>
auto parse(continuation_type &&continuation, from_type &&from)
{
  using from_type_ = std::decay_t<from_type>;
  using char_type = b::detected_or_t<char, char_t, from_type_>;
  using traits_type = b::detected_or_t<std::char_traits<char_type>, traits_t, from_type_>;
  if constexpr(std::is_base_of_v<std::basic_istream<char_type, traits_type>, from_type_>)
  {
    boost::spirit::basic_istream_iterator<char_type, traits_type> begin {std::forward<from_type>(from)}, end;
    return parse(std::forward<continuation_type>(continuation), begin, end);
  }
  else
    return parse(std::forward<continuation_type>(continuation), from.begin(), from.end());
}

struct properties final
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
#if defined(_DEBUG)
  template<typename key_type, typename mapped_type>
  using map_type = std::unordered_map<key_type, mapped_type, decltype(hash), decltype(equals_to)>;
#else  // defined(_DEBUG)
  template<typename key_type, typename mapped_type>
  using map_type = robin_hood::unordered_flat_map<key_type, mapped_type, decltype(hash), decltype(equals_to)>;
#endif // defined(_DEBUG)

  using object_type = map_type<field_name_type, config::value_type>;
  using world_type = map_type<object_name_type, object_type>;

  struct walker final
  {
    const auto &get() const noexcept { return value; }

    operator const config::value_type &() const noexcept { return value; }

    operator bool() const noexcept
    {
      return std::visit(boilerplate::overloaded {
                          [&](const config::string_type &alternative) { return alternative == "true" || resolve(); },
                          [&](const config::numeric_type &alternative) { return bool(alternative); },
                          [&](const config::string_list_type &alternative) { return !alternative.empty(); },
                          [&](const config::numeric_list_type &alternative) { return !alternative.empty(); },
                          [&](const auto &) { return false; },
                        },
                        value);
    }

    operator const config::string_type &() const noexcept
    {
      return std::visit(boilerplate::overloaded {
                          [&](const config::string_type &alternative) { return std::cref(alternative); },
                          [&]([[maybe_unused]] const auto &) {
                            static const config::string_type result;
                            return std::cref(result);
                          },
                        },
                        value);
    }

    template<typename value_type>
    operator value_type() const noexcept requires std::is_arithmetic_v<value_type>
    {
      return std::holds_alternative<config::numeric_type>(value) ? static_cast<value_type>(std::get<config::numeric_type>(value)) : value_type {};
    }

    walker operator[](const std::size_t &index) const noexcept
    {
      return std::visit(
        [&](const auto &alternative) -> walker {
          if constexpr(std::is_same_v<std::decay_t<decltype(alternative)>,
                                      config::string_list_type> || std::is_same_v<std::decay_t<decltype(alternative)>, config::numeric_list_type>)
            return {data, alternative[index]};
          else
            return {data};
        },
        value);
    }

    walker operator[](const field_name_type &index) const noexcept
    {
      if(!resolve())
        return {data};
      if(auto it = object->find(index); it != object->end())
        return {data, it->second};
      return {data};
    }

    walker operator[](const hashed_string &index) const noexcept
    {
      return (*this)[field_name_type(index.data())]; // TODO
    }

    bool resolve() const
    {
      if(object)
        return true;
      if(!std::holds_alternative<config::string_type>(value))
        return false;
      if(auto it = data->find(object_name_type(std::get<config::string_type>(value))); it != data->end())
        object.reset(&it->second);
      return !!object;
    }

    gsl::strict_not_null<std::shared_ptr<const world_type>> data;
    config::value_type value {};
    mutable boilerplate::maybe_null_observer_ptr<const object_type> object {};
  };

  template<typename input_type>
  static leaf::result<properties> create(input_type &&input) noexcept
  {
    auto data = std::make_shared<world_type>();
    BOOST_LEAF_CHECK(config::parse(
      [&](auto object, auto field, auto &&value) {
        auto [it, _] = data->try_emplace(object_name_type {object.begin(), object.end()}, object_type {});
        it->second.insert_or_assign(field_name_type {field.begin(), field.end()}, std::forward<decltype(value)>(value));
      },
      std::forward<input_type>(input)));
    return {properties {std::move(data)}};
  }

  walker operator[](const object_name_type &index) const noexcept
  {
    if(auto it = data->find(index); it != data->end())
      return {gsl::make_not_null(data), it->first, boilerplate::make_strict_not_null(&it->second)};
    return {gsl::make_not_null(data)};
  }

  walker operator[](const hashed_string &index) const noexcept
  {
    return (*this)[object_name_type(index.data())]; // TODO
  }

  walker operator[](const std::tuple<object_name_type, field_name_type> &index) const noexcept
  {
    const auto &[object, field] = index;
    return (*this)[object][field];
  }

  std::shared_ptr<const world_type> data {};

private:
  static value_type convert(const config::value_type &value) noexcept
  {
    return std::visit([](const auto &alternative) { return value_type(alternative); }, value);
  }
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
      leaf::try_handle_all(
        [&]() -> leaf::result<void> {
          const auto result = BOOST_LEAF_TRYX(config::properties::create(config_str));
          const auto a = result["a"_hs];
          CHECK(a);
          CHECK(a["a"_hs].get() == config::value_type {"string"s});
          CHECK(a["b"_hs].get() == config::value_type {42.0});
          CHECK(a["c"_hs].get() == config::value_type {std::vector {"string"s, "list"s}});
          CHECK(a["d"_hs].get() == config::value_type {std::vector {0.0, 1.0, 2.0, 3.0, 4.0}});
          return {};
        },
        [&]([[maybe_unused]] const leaf::error_info &unmatched) { CHECK(false); });
    }
    SUBCASE("failure")
    {
      const auto config_str = "\
parsing.fails <- here;\n\n"sv;
      bool success = false;
      leaf::try_handle_all(
        [&]() -> leaf::result<void> {
          BOOST_LEAF_CHECK(config::properties::create(config_str));
          return {};
        },
        [&](const config::parse_error &error) {
          CHECK(error.indices == std::pair {std::size_t {17}, std::size_t {24}});
          CHECK(error.which == "value");
          success = true;
        },
        [&]([[maybe_unused]] const leaf::error_info &unmatched) { CHECK(false); });
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
      leaf::try_handle_all(
        [&]() -> leaf::result<void> {
          const auto result = BOOST_LEAF_TRYX(config::properties::create(config_str));
          const auto a = result["a"_hs];
          CHECK(a);
          CHECK(a.get() == config::value_type {"a"s});
          CHECK(a["value"_hs].get() == config::value_type {1.0});
          const auto b = a["next"_hs];
          CHECK(b);
          CHECK(b.get() == config::value_type {"b"s});
          CHECK(b["value"_hs].get() == config::value_type {"string"s});
          const auto c = b["next"_hs];
          CHECK(c);
          CHECK(c.get() == config::value_type {"c"s});
          const auto a_ = c["next"_hs][0];
          const auto b_ = c["next"_hs][1];
          CHECK(a_);
          CHECK(b_);
          CHECK(b_.get() == config::value_type {"b"s});
          CHECK(b_["next"_hs].get() == config::value_type {"c"s});

          return {};
        },
        [&]([[maybe_unused]] const leaf::error_info &unmatched) { CHECK(false); });
    }
  }
}
// GCOVR_EXCL_STOP
#endif // defined(DOCTEST_LIBRARY_INCLUDED)
