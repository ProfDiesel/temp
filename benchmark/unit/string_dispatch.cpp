#include <benchmark/benchmark.h>

#include <boost/preprocessor/facilities/expand.hpp>
#include <boost/preprocessor/facilities/identity.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/tuple/elem.hpp>

#include <frozen/string.h>
#include <frozen/unordered_map.h>
#include <frozen/unordered_set.h>

#include <robin_hood.h>
#include <std_function/function.h>

#include <cstring>
#include <functional>
#include <random>
#include <tuple>
#include <unordered_map>
#include <vector>

struct caseless_ascii_char_traits : std::char_traits<char>
{
  static constexpr char to_lower(char c) noexcept { return ((c >= 'A') && (c <= 'Z')) ? c + 'a' - 'A' : c; }

  static constexpr bool eq(char c0, char c1) noexcept { return to_lower(c0) == to_lower(c1); }
  static constexpr bool lt(char c0, char c1) noexcept { return to_lower(c0) < to_lower(c1); }

  static constexpr int compare(const char *s0, const char *s1, std::size_t n) noexcept
  {
    while(n--)
    {
      if(to_lower(*s0) < to_lower(*s1))
        return -1;
      if(to_lower(*s0) > to_lower(*s1))
        return 1;
      ++s0;
      ++s1;
    }
    return 0;
  }

  static constexpr const char *find(const char *s, int n, char c) noexcept
  {
    c = to_lower(c);
    while(n--)
    {
      if(to_lower(*s) == c)
        return s;
      ++s;
    }
    return nullptr;
  }
};

struct caseless_ascii_xxhasher
{
  constexpr std::size_t operator()(const char *data, std::size_t length, uint64_t seed = 0) const noexcept { return std::size_t(hash(data, length, seed)); }

  template<typename value_type>
  constexpr std::size_t operator()(const value_type &value, std::size_t seed = 0) const noexcept
  {
    return (*this)(value.data(), value.size(), seed);
  }

  /*
  template<typename iterator_type>
    std::size_t operator()(iterator_type begin, std::size_t length) const noexcept
    {
      static thread_local char data[64];
      std::size_t n = std::min(length, std::size_t(64));
      std::copy_n(begin, n, data);
      return (*this)((const char *)data, n);
    }
*/
private:
  static constexpr uint8_t to_lower(const char c) noexcept { return ((c >= 'A') && (c <= 'Z')) ? uint8_t(c + 'a' - 'A') : uint8_t(c); }

  //
  // from Compile-time xxhash by Daniel Kirchner (https://github.com/ekpyron/xxhashct.git)
  // made case insensitive

  static constexpr uint64_t hash(const char *p, uint64_t len, uint64_t seed) noexcept
  {
    return finalize((len >= 32 ? h32bytes(p, len, seed) : seed + PRIME5) + len, p + (len & ~0x1Fu), len & 0x1Fu);
  }

  static constexpr auto PRIME1 = 11400714785074694791ULL;
  static constexpr auto PRIME2 = 14029467366897019727ULL;
  static constexpr auto PRIME3 = 1609587929392839161ULL;
  static constexpr auto PRIME4 = 9650029242287828579ULL;
  static constexpr auto PRIME5 = 2870177450012600261ULL;

  static constexpr uint64_t rotl(uint64_t x, int r) noexcept { return ((x << r) | (x >> (64 - r))); }
  static constexpr uint64_t mix1(const uint64_t h, const uint64_t prime, int rshift) noexcept { return (h ^ (h >> rshift)) * prime; }
  static constexpr uint64_t mix2(const uint64_t p, const uint64_t v = 0) noexcept { return rotl(v + p * PRIME2, 31) * PRIME1; }
  static constexpr uint64_t mix3(const uint64_t h, const uint64_t v) noexcept { return (h ^ mix2(v)) * PRIME1 + PRIME4; }
  static constexpr uint64_t fetch64(const char *p, const uint64_t v = 0) noexcept
  {
    return mix2(uint64_t(to_lower(p[0])) | (uint64_t(to_lower(p[1])) << 8) | (uint64_t(to_lower(p[2])) << 16) | (uint64_t(to_lower(p[3])) << 24)
                  | (uint64_t(to_lower(p[4])) << 32) | (uint64_t(to_lower(p[5])) << 40) | (uint64_t(to_lower(p[6])) << 48) | (uint64_t(to_lower(p[7])) << 56),
                v);
  }
  static constexpr uint64_t fetch32(const char *p) noexcept
  {
    return uint64_t(uint32_t(to_lower(p[0])) | (uint32_t(to_lower(p[1])) << 8) | (uint32_t(to_lower(p[2])) << 16) | (uint32_t(to_lower(p[3])) << 24)) * PRIME1;
  }
  static constexpr uint64_t fetch8(const char *p) noexcept { return to_lower(p[0]) * PRIME5; }
  static constexpr uint64_t finalize(const uint64_t h, const char *p, uint64_t len) noexcept
  {
    return (len >= 8)
             ? (finalize(rotl(h ^ fetch64(p), 27) * PRIME1 + PRIME4, p + 8, len - 8))
             : ((len >= 4) ? (finalize(rotl(h ^ fetch32(p), 23) * PRIME2 + PRIME3, p + 4, len - 4))
                           : ((len > 0) ? (finalize(rotl(h ^ fetch8(p), 11) * PRIME1, p + 1, len - 1)) : (mix1(mix1(mix1(h, PRIME2, 33), PRIME3, 29), 1, 32))));
  }
  static constexpr uint64_t h32bytes(const char *p, uint64_t len, const uint64_t v1, const uint64_t v2, const uint64_t v3, const uint64_t v4) noexcept
  {
    return (len >= 32) ? h32bytes(p + 32, len - 32, fetch64(p, v1), fetch64(p + 8, v2), fetch64(p + 16, v3), fetch64(p + 24, v4))
                       : mix3(mix3(mix3(mix3(rotl(v1, 1) + rotl(v2, 7) + rotl(v3, 12) + rotl(v4, 18), v1), v2), v3), v4);
  }
  static constexpr uint64_t h32bytes(const char *p, uint64_t len, const uint64_t seed) noexcept
  {
    return h32bytes(p, len, seed + PRIME1 + PRIME2, seed + PRIME2, seed, seed - PRIME1);
  }
};

template<typename function_type, typename tuple_type, size_t current_index = 0>
inline int apply_at_index(function_type function, tuple_type &tuple, size_t index)
{
  if constexpr(current_index < std::tuple_size_v<tuple_type>)
    return current_index == index ? std::invoke(function, std::get<current_index>(tuple))
                                  : apply_at_index<function_type, tuple_type, current_index + 1>(function, tuple, index);
  return 0;
}

//
// (echo '#define STRINGS \'; cat /etc/dictionaries-common/words | grep -v "'" | head -256 | xargs -L1 -I {} echo '(("'{}'", 0)) \'; echo) >
// strings_declaration.h
#include "strings_declaration.h"

using namespace frozen::string_literals;

#define HANDLE_STRING(r, data, elem) BOOST_PP_CAT(BOOST_PP_TUPLE_ELEM(0, elem), _s),
constexpr std::array<frozen::string, BOOST_PP_SEQ_SIZE(PIPO_STRINGS)> strings = {BOOST_PP_SEQ_FOR_EACH(HANDLE_STRING, _, PIPO_STRINGS)};
#undef HANDLE_STRING

#define HANDLE_STRING_COMMON(r, data, elem)                                                                                                                    \
  std::pair {BOOST_PP_CAT(BOOST_PP_TUPLE_ELEM(0, elem), _s), +[](const key_type &k, volatile salt_type &x) { return combine(k, x); }},

using salt_type = int;

template<typename key_type>
int combine(key_type k, salt_type x) noexcept
{
  return int(k.size() + x);
}

template<uint_fast32_t n>
struct lcg
{
  static constexpr uint_fast32_t m = 2147483647;
  static constexpr uint_fast32_t a = 48271;
  static constexpr uint_fast32_t c = 0;
  static constexpr uint_fast32_t seed = 0;
  static constexpr uint_fast32_t previous = []() constexpr
  {
    if constexpr(n > 0)
      return lcg<n - 1>::value;
    else
      return seed;
  }
  ();
  static constexpr uint_fast32_t value = c + a * previous & m;
};

template<uint_fast32_t n>
constexpr auto lcg_v = lcg<n>::value;

// template<std::size_t... indices>
// constexpr auto _shuffled(std::index_sequence<indices...>) { return std::array<frozen::string, sizeof...(indices)> { (strings[lcg_v<indices> %
// strings.size()])...}; }; constexpr auto shuffled_strings = _shuffled(std::make_index_sequence<10>());

template<std::size_t... indices>
auto _shuffled(std::index_sequence<indices...>)
{
  static_assert(!strings.empty());
  return std::vector<frozen::string> {(strings[lcg_v<indices> % strings.size()])...};
};
auto shuffled_strings = _shuffled(std::make_index_sequence<10>());

template<typename map_type>
void bench(map_type &&map) noexcept
{
  volatile salt_type salt;

  for(auto &&k: shuffled_strings)
    benchmark::DoNotOptimize(std::invoke(map.find(k)->second, k, salt));
}

static void do_nothing(benchmark::State &state) noexcept
{
  volatile salt_type x = 32;

  for(auto _: state)
  {
    for(auto &&k: shuffled_strings)
      benchmark::DoNotOptimize(combine(k, x));
  }
}
BENCHMARK(do_nothing);

static void std_std(benchmark::State &state) noexcept
{
  using key_type = frozen::string;
  using mapped_type = std::function<int(const key_type &, volatile salt_type &)>;
  using map_type = std::unordered_map<key_type, mapped_type>;

  static const map_type map {{BOOST_PP_SEQ_FOR_EACH(HANDLE_STRING_COMMON, _, PIPO_STRINGS)}};
  for(auto _: state)
    bench(map);
}
BENCHMARK(std_std);

static void rhd_std(benchmark::State &state) noexcept
{
  using namespace frozen::string_literals;
  using key_type = frozen::string;
  using mapped_type = std::function<int(const key_type &, volatile salt_type &)>;
  using map_type = robin_hood::unordered_flat_map<key_type, mapped_type, caseless_ascii_xxhasher>;

  static const map_type map {{BOOST_PP_SEQ_FOR_EACH(HANDLE_STRING_COMMON, _, PIPO_STRINGS)}};
  for(auto _: state)
    bench(map);
}
BENCHMARK(ska_std);

static void frz_std(benchmark::State &state) noexcept
{
  using namespace frozen::string_literals;
  using key_type = frozen::string;
  using mapped_type = std::function<int(const key_type &, volatile salt_type &)>;
  using map_type = frozen::unordered_map<key_type, mapped_type, strings.size()>;

  static const map_type map {{BOOST_PP_SEQ_FOR_EACH(HANDLE_STRING_COMMON, _, PIPO_STRINGS)}};
  for(auto _: state)
    bench(map);
}
BENCHMARK(frz_std);

static void frz_ska(benchmark::State &state) noexcept
{
  using namespace frozen::string_literals;
  using key_type = frozen::string;
  using mapped_type = func::function<int(const key_type &, volatile salt_type &)>;
  using map_type = frozen::unordered_map<key_type, mapped_type, strings.size()>;

  map_type map {{BOOST_PP_SEQ_FOR_EACH(HANDLE_STRING_COMMON, _, PIPO_STRINGS)}};
  for(auto _: state)
    bench(map);
}
BENCHMARK(frz_ska);

static void frz_ptr(benchmark::State &state) noexcept
{
  using namespace frozen::string_literals;
  using key_type = frozen::string;
  using mapped_type = int (*)(const key_type &, volatile int &);
  using map_type = frozen::unordered_map<key_type, mapped_type, strings.size()>;

  constexpr map_type map {{BOOST_PP_SEQ_FOR_EACH(HANDLE_STRING_COMMON, _, PIPO_STRINGS)}};
  for(auto _: state)
    bench(map);
}
BENCHMARK(frz_ptr);

static void frz_ptr_xxhash(benchmark::State &state) noexcept
{
  using namespace frozen::string_literals;
  using key_type = frozen::string;
  using mapped_type = int (*)(const key_type &, volatile int &);
  using map_type = frozen::unordered_map<key_type, mapped_type, strings.size(), caseless_ascii_xxhasher>;

  constexpr map_type map {{BOOST_PP_SEQ_FOR_EACH(HANDLE_STRING_COMMON, _, PIPO_STRINGS)}};
  for(auto _: state)
    bench(map);
}
BENCHMARK(frz_ptr_xxhash);

static void rhd_ska_capture(benchmark::State &state) noexcept
{
  using namespace frozen::string_literals;
  using key_type = frozen::string;
  using mapped_type = func::function<int(const key_type &)>;
  using map_type = robin_hood::unordered_flat_map<key_type, mapped_type, caseless_ascii_xxhasher>;

  volatile salt_type x = 32;
  for(auto _: state)
  {
#define HANDLE_STRING(r, data, elem) std::pair {BOOST_PP_CAT(BOOST_PP_TUPLE_ELEM(0, elem), _s), [&](const key_type &k) { return combine(k, x); }},
    map_type map {{BOOST_PP_SEQ_FOR_EACH(HANDLE_STRING, _, PIPO_STRINGS)}};
#undef HANDLE_STRING

    for(auto &&k: shuffled_strings)
      benchmark::DoNotOptimize(std::invoke(map.find(k)->second, k));
  }
}
BENCHMARK(ska_ska_capture);

namespace helpers
{
// perfect hash for known strings
struct hash
{
#define HANDLE_STRING(r, data, elem) BOOST_PP_TUPLE_ELEM(0, elem),
  static constexpr frozen::unordered_set<frozen::string, strings.size()> known_values {{BOOST_PP_SEQ_FOR_EACH(HANDLE_STRING, _, PIPO_STRINGS)}};
#undef HANDLE_STRING
  static constexpr std::size_t UNKNOWN = known_values.size();

  constexpr std::size_t operator()(const frozen::string &s) const noexcept { return known_values.find(s) - known_values.begin(); }
};

template<typename char_type, char_type... c>
constexpr auto operator"" _h()
{
  constexpr char data[] = {c..., '\0'};
  constexpr auto result = hash()(frozen::string {data});
  static_assert(result != hash::UNKNOWN);
  return result;
}
} // namespace helpers

static void frz_switch(benchmark::State &state) noexcept
{
  using namespace helpers;

  volatile salt_type x = 32;

  for(auto _: state)
  {
    for(auto &&k: shuffled_strings)
    {
      volatile std::size_t i = hash()(k);
      benchmark::DoNotOptimize(i);
      asm("# -- switch block --");
      switch(i)
      {
#define HANDLE_STRING(r, data, elem)                                                                                                                           \
  case BOOST_PP_CAT(BOOST_PP_TUPLE_ELEM(0, elem), _h): benchmark::DoNotOptimize(combine(k, x)); break;
        BOOST_PP_SEQ_FOR_EACH(HANDLE_STRING, _, PIPO_STRINGS);
#undef HANDLE_STRING
      }
    }
  }
}
BENCHMARK(frz_switch);

static void frz_ptr_array(benchmark::State &state) noexcept
{
  using namespace frozen::string_literals;
  using key_type = frozen::string;
  using mapped_type = int (*)(const key_type &, volatile int &) noexcept;

  volatile salt_type x = 32;

  constexpr auto constexpr_id = [](const frozen::string &k) constexpr noexcept
  {
#define HANDLE_STRING(r, data, elem) BOOST_PP_TUPLE_ELEM(0, elem),
    constexpr frozen::unordered_set<frozen::string, strings.size()> buckets {{BOOST_PP_SEQ_FOR_EACH(HANDLE_STRING, _, PIPO_STRINGS)}};
#undef HANDLE_STRING
    return buckets.find(k) - buckets.begin();
  };

  const auto id = [](const frozen::string &k) noexcept
  {
#define HANDLE_STRING(r, data, elem) BOOST_PP_TUPLE_ELEM(0, elem),
    static const frozen::unordered_set<frozen::string, strings.size()> buckets {{BOOST_PP_SEQ_FOR_EACH(HANDLE_STRING, _, PIPO_STRINGS)}};
#undef HANDLE_STRING
    return buckets.find(k) - buckets.begin();
  };

  constexpr auto func_array = [&]() constexpr noexcept
  {
    std::array<mapped_type, BOOST_PP_SEQ_SIZE(PIPO_STRINGS)> result;
#define HANDLE_STRING(r, data, elem)                                                                                                                           \
  result[constexpr_id(BOOST_PP_CAT(BOOST_PP_TUPLE_ELEM(0, elem), _s))] = +[](const key_type &k, volatile salt_type &x) noexcept { return combine(k, x); };
    BOOST_PP_SEQ_FOR_EACH(HANDLE_STRING, _, PIPO_STRINGS)
#undef HANDLE_STRING
    return result;
  }
  ();

  for(auto _: state)
  {
    for(auto &&k: shuffled_strings)
    {
      volatile std::size_t i = id(k);
      benchmark::DoNotOptimize(i);
      asm("# -- jump table --");
      benchmark::DoNotOptimize(func_array[i](k, x));
    }
  }
}
BENCHMARK(frz_ptr_array);

BENCHMARK_MAIN();

/*
template<std::tuple<frozen::string, int(*)(frozen::string)>... pairs>
static auto dispatch(const frozen::string &key)
{
  constexpr auto N = sizeof...(pairs);
  static_assert(N == 2);
  constexpr frozen::unordered_set<frozen::string, N> buckets {  std::get<0>(pairs)... };
  constexpr std::tuple functions { std::get<1>(pairs)..., [&](auto) { return -1; }};
  return apply_at_index([&](auto func) { return std::invoke(func, key); }, functions, buckets.find(key) - buckets.begin());
}
*/
