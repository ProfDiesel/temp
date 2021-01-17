#pragma once

#include <frozen/string.h>
#include <frozen/unordered_set.h>

#include <string_view>

namespace dispatch
{
// perfect hash for known strings
struct hash
{
  //#define HANDLE_STRING(r, data, elem) BOOST_PP_TUPLE_ELEM(0, elem),
  //  static constexpr frozen::unordered_set<frozen::string, strings.size()> known_values {{BOOST_PP_SEQ_FOR_EACH(HANDLE_STRING, _, ENTRY_TYPES)}};
  //#undef HANDLE_STRING
  static constexpr frozen::unordered_set<frozen::string, 4> known_values {"payload", "subscribe", "unsubscribe", "quit"};

  static constexpr std::size_t UNKNOWN = known_values.size();

  constexpr std::size_t operator()(const frozen::string &s) const noexcept { return std::size_t(known_values.find(s) - known_values.begin()); }
  constexpr std::size_t operator()(std::string_view s) const noexcept { return (*this)(frozen::string(s.data(), s.size())); }
};

namespace literals
{
template<typename char_type, char_type... c>
constexpr auto operator"" _h()
{
  constexpr char data[] = {c..., '\0'};
  constexpr auto result = hash()(frozen::string {data});
  static_assert(result != hash::UNKNOWN);
  return result;
}
} // namespace literals
} // namespace dispatch

#if defined(DOCTEST_LIBRARY_INCLUDED)
// GCOVR_EXCL_START

TEST_SUITE("properties_dispatch")
{
  using namespace dispatch::literals;

  constexpr auto f = [](std::string_view s) {
    switch(dispatch::hash()(s))
    {
    case "payload"_h: return 0;
    case "subscribe"_h: return 1;
    case "unsubscribe"_h: return 2;
    case "quit"_h: return 3;
    }
    return 4;
  };

  TEST_CASE("simple")
  {
    CHECK(f("payload") == 0);
    CHECK(f("subscribe") == 1);
    CHECK(f("unsubscribe") == 2);
    CHECK(f("quit") == 3);
    CHECK(f("not_existing") == 4);
  }
}

// GCOVR_EXCL_STOP
#endif // defined(DOCTEST_LIBRARY_INCLUDED)
