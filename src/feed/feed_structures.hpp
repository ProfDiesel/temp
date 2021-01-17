#pragma once

#include "feed_fields.hpp"

#include <boilerplate/boilerplate.hpp>
#if !defined(LEAN_AND_MEAN)
#include <boilerplate/units.hpp>
#endif // !defined(LEAN_AND_MEAN)

#include <boost/endian/buffers.hpp>

#include <boost/preprocessor/facilities/expand.hpp>
#include <boost/preprocessor/facilities/identity.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/tuple/elem.hpp>

#if !defined(LEAN_AND_MEAN)
#if defined(__clang__)
// TODO: use Intel RDFP Math library
#else // defined(__clang__)
#  include <decimal/decimal>
#endif // defined(__clang__)
#endif // !defined(LEAN_AND_MEAN)

namespace feed
{
namespace endian = boost::endian;

#if defined(LEAN_AND_MEAN)
using price_t = float;
#elif defined(__clang__) // defined(LEAN_AND_MEAN)
using price_t = float;
#else  // defined(__clang__)
using price_t = units::make_quantity_type<struct price_dimension, std::decimal::decimal32>;
#endif // defined(__clang__)
using quantity_t = std::uint32_t;
using instrument_id_type = std::uint16_t;
using sequence_id_type = std::uint32_t;

namespace literals
{
#if defined(LEAN_AND_MEAN)
  inline price_t operator""_p(long double value) { return static_cast<price_t>(value); };
#elif defined(__clang__) // defined(LEAN_AND_MEAN)
  inline price_t operator""_p(long double value) { return {value}; };
#else  // defined(__clang__)
  inline price_t operator""_p(long double value) { return {reinterpret_cast<std::decimal::decimal32::__decfloat32 &>(value)}; };
#endif // defined(__clang__)
}

struct update final
{
  const std::uint8_t field = 0;
  const std::uint32_t value = 0;
} __attribute__((packed));
static_assert(sizeof(update) == 5); // NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

struct message final
{
  const endian::big_uint16_buf_t instrument {};
  const endian::big_uint32_buf_t sequence_id {};

  std::uint8_t nb_updates = 0;
  struct update update = {};
} __attribute__((packed));
static_assert(sizeof(message) == 12); // NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

// clang-format off
#define DECLARE_ENUM(r, data, elem) BOOST_PP_TUPLE_ELEM(0, elem) = BOOST_PP_TUPLE_ELEM(1, elem),
enum struct field : std::uint8_t { BOOST_PP_SEQ_FOR_EACH(DECLARE_ENUM, _, FEED_FIELDS) };
#undef DECLARE_ENUM

template<field value>
using field_c = std::integral_constant<field, value>;

#define DECLARE_CONSTANT(r, data, elem) using BOOST_PP_CAT(BOOST_PP_TUPLE_ELEM(0, elem), _c) = field_c<field::BOOST_PP_TUPLE_ELEM(0, elem)>;
BOOST_PP_SEQ_FOR_EACH(DECLARE_CONSTANT, _, FEED_FIELDS)
#undef DECLARE_CONSTANT

template<field>
struct field_type;

#define DECLARE_CONSTANT(r, data, elem) template<> struct field_type<field::BOOST_PP_TUPLE_ELEM(0, elem)> { using type = BOOST_PP_TUPLE_ELEM(2, elem); };
BOOST_PP_SEQ_FOR_EACH(DECLARE_CONSTANT, _, FEED_FIELDS)
#undef DECLARE_CONSTANT

template<field value>
using field_type_t = typename field_type<value>::type;

enum struct field_index
{
#define DECLARE_ENUM_INDEX(r, data, elem) BOOST_PP_TUPLE_ELEM(0, elem),
  BOOST_PP_SEQ_FOR_EACH(DECLARE_ENUM_INDEX, _, FEED_FIELDS)
#undef DECLARE_ENUM_INDEX
  _count
};

namespace detail
{

template<typename value_type>
value_type read_value(const struct update &update) noexcept
{
  return endian::big_to_native(update.value);
}

template<>
inline price_t read_value<price_t>(const struct update &update) noexcept
{
  auto value = read_value<std::uint32_t>(update);
#if !defined(LEAN_AND_MEAN) && !defined(__clang__)
  return price_t {std::decimal::decimal32 {reinterpret_cast<std::decimal::decimal32::__decfloat32 &>(value)}};
#else  // !defined(LEAN_AND_MEAN) && !defined(__clang__)
  return price_t {reinterpret_cast<float &>(value)};
#endif // !defined(LEAN_AND_MEAN) && !defined(__clang__)
}

template<typename value_type>
void encode_update(enum field field, const value_type &value, struct update &update) noexcept
{
  using struct_update = struct update;
  new(&update) struct_update {.field = static_cast<uint8_t>(field), .value = endian::native_to_big(value)};
}

template<>
inline void encode_update(enum field field, const price_t &value, struct update &update) noexcept
{
  std::uint32_t result;
#if defined(LEAN_AND_MEAN)
  reinterpret_cast<float &>(result) = value;
#elif defined(__clang__) // defined(LEAN_AND_MEAN)
  reinterpret_cast<float &>(result) = value.get();
#else  // defined(__clang__)
  reinterpret_cast<std::decimal::decimal32::__decfloat32 &>(result) = const_cast<std::decimal::decimal32 &>(value.get()).__getval();
#endif // defined(__clang__)
  encode_update(field, result, update);
}

} // namespace detail

template<typename continuation_type>
[[using gnu : always_inline, flatten, hot]] void visit_update(continuation_type &&continuation, const struct update &update)
{
  switch(field{update.field})
  {
    // clang-format off
#define HANDLE_FIELD(r, _, elem) \
  case field::BOOST_PP_TUPLE_ELEM(0, elem): \
    continuation(BOOST_PP_CAT(BOOST_PP_TUPLE_ELEM(0, elem), _c){}, detail::read_value<field_type_t<field::BOOST_PP_TUPLE_ELEM(0, elem)>>(update)); \
    break;
  BOOST_PP_SEQ_FOR_EACH(HANDLE_FIELD, _, FEED_FIELDS)
#undef HANDLE_FIELD
    // clang-format on
  }
}

struct instrument_state final
{
#define DECLARE_FIELD(r, data, elem) BOOST_PP_TUPLE_ELEM(2, elem) BOOST_PP_TUPLE_ELEM(0, elem);
  BOOST_PP_SEQ_FOR_EACH(DECLARE_FIELD, _, FEED_FIELDS)
#undef DECLARE_FIELD
  std::bitset<BOOST_PP_SEQ_SIZE(FEED_FIELDS)> updates;
  sequence_id_type sequence_id = 0;
};

template<typename value_type>
[[using gnu : always_inline, flatten, hot]] inline void update_state(instrument_state &state, field field, value_type &&value) noexcept
{
  switch(field)
  {
  // clang-format off
#define HANDLE_FIELD(r, _, elem) \
  case field::BOOST_PP_TUPLE_ELEM(0, elem): \
    state.BOOST_PP_TUPLE_ELEM(0, elem) = value; \
    state.updates.set(static_cast<std::size_t>(field_index::BOOST_PP_TUPLE_ELEM(0, elem))); \
    break;
  BOOST_PP_SEQ_FOR_EACH(HANDLE_FIELD, _, FEED_FIELDS)
  }
#undef HANDLE_FIELD
  // clang-format on
}

[[using gnu : always_inline, flatten, hot]] inline void update_state(instrument_state &state, const update &update) noexcept
{
  visit_update([&state](auto field, auto &&value) { update_state(state, field, std::move(value)); }, update);
}

[[using gnu : always_inline, flatten, hot]] inline void update_state(instrument_state &state, const message &message) noexcept
{
  for(const auto *update = &message.update, *_end = update + message.nb_updates; update != _end; ++update)
    update_state(state, *update);
}

template<typename continuation_type>
[[using gnu : always_inline, flatten, hot]] void visit_state(continuation_type &&continuation, const instrument_state &state)
{
    // clang-format off
#define HANDLE_FIELD(r, _, elem) \
  if(state.updates[static_cast<std::size_t>(field_index::BOOST_PP_TUPLE_ELEM(0, elem))]) continuation(BOOST_PP_CAT(BOOST_PP_TUPLE_ELEM(0, elem), _c){}, state.BOOST_PP_TUPLE_ELEM(0, elem));
  BOOST_PP_SEQ_FOR_EACH(HANDLE_FIELD, _, FEED_FIELDS)
#undef HANDLE_FIELD
  // clang-format on
}
} // namespace feed

