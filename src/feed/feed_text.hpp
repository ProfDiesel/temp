#pragma once

#include <boilerplate/boilerplate.hpp>
#include <boilerplate/chrono.hpp>
#include <boilerplate/leaf.hpp>
#include <boilerplate/pointers.hpp>
#include <boilerplate/units.hpp>
#include <boilerplate/x3.hpp>

#include <asio/coroutine.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/write.hpp>

#include <boost/fusion/include/adapt_struct.hpp>

#include <boost/hof/lazy.hpp>
#include <boost/hof/lift.hpp>

#include <boost/preprocessor/facilities/expand.hpp>
#include <boost/preprocessor/facilities/identity.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/tuple/elem.hpp>

#include <std_function/function.h>

#include <bitset>
#include <cstdint>
#if !defined(__clang__)
#  include <decimal/decimal>
#endif // !defined(__clang__)

namespace feed
{
namespace hof = boost::hof;
namespace leaf = boost::leaf;

// using price_t = make_quantity_type<struct price_dimension, std::decimal::decimal32>;
using price_t = units::make_quantity_type<struct price_dimension, float>;
using quantity_t = std::uint32_t;
using instrument_id_type = std::uint16_t;
using sequence_id_type = std::uint32_t;

namespace detail
{
struct update final
{
  std::uint8_t field {};
  long double value {};
};

struct message final
{
  instrument_id_type instrument {};
  sequence_id_type sequence_id {};
  std::vector<update> updates {};
};

template<typename update_type, typename value_type>
struct value_proxy
{
  using update_ptr = boilerplate::not_null_observer_ptr<update_type>;
  update_ptr update;

  explicit value_proxy(update_ptr update): update(update) {}
  operator value_type() const noexcept { return static_cast<value_type>(update->value); }
  auto &operator=(const value_type &value) noexcept { update->value = std::decay_t<decltype(update->value)> {value}; };
};

// TODO
template<typename update_type, typename value_type>
struct value_proxy<const update_type &, value_type>
{
  using update_ptr = boilerplate::not_null_observer_ptr<const struct update>;
  update_ptr update;

  explicit value_proxy(update_ptr update): update(update) {}
  operator value_type() const noexcept { return value_type {update->value}; }
};

#include "feed_declarations.hpp"

} // namespace detail

} // namespace feed

BOOST_FUSION_ADAPT_STRUCT(feed::detail::update, field, value)
BOOST_FUSION_ADAPT_STRUCT(feed::detail::message, instrument, sequence_id, updates)

namespace feed
{
// clang-format off
#define DECLARE_ENUM(r, data, elem) BOOST_PP_TUPLE_ELEM(0, elem) = BOOST_PP_TUPLE_ELEM(1, elem),
enum struct field : std::uint8_t { BOOST_PP_SEQ_FOR_EACH(DECLARE_ENUM, _, FEED_FIELDS) };
#undef DECLARE_ENUM

template<field value>
using field_c = std::integral_constant<field, value>;

#define DECLARE_CONSTANT(r, data, elem) using BOOST_PP_CAT(BOOST_PP_TUPLE_ELEM(0, elem), _c) = field_c<field::BOOST_PP_TUPLE_ELEM(0, elem)>;
BOOST_PP_SEQ_FOR_EACH(DECLARE_CONSTANT, _, FEED_FIELDS)
#undef DECLARE_CONSTANT

namespace detail
{
#define DECLARE_ENUM_INDEX(r, data, elem) BOOST_PP_TUPLE_ELEM(0, elem),
enum struct field_index
{
  BOOST_PP_SEQ_FOR_EACH(DECLARE_ENUM_INDEX, _, FEED_FIELDS)
};
#undef DECLARE_ENUM_INDEX

template<typename result_type=void, typename continuation_type, typename update_type>
HOTPATH result_type visit_update(continuation_type &&continuation, update_type &&update) noexcept
{
  switch(update.field)
  {
    // clang-format off
#define HANDLE_FIELD(r, _, elem) \
  case BOOST_PP_TUPLE_ELEM(1, elem): return continuation(BOOST_PP_CAT(BOOST_PP_TUPLE_ELEM(0, elem), _c){}, value_proxy<std::remove_reference_t<update_type>, BOOST_PP_TUPLE_ELEM(2, elem)>(boilerplate::make_strict_not_null(&update))); break;
  BOOST_PP_SEQ_FOR_EACH(HANDLE_FIELD, _, FEED_FIELDS)
#undef HANDLE_FIELD
    // clang-format on
  }
  return result_type {};
}

#define DECLARE_FIELD(r, data, elem) BOOST_PP_TUPLE_ELEM(2, elem) BOOST_PP_TUPLE_ELEM(0, elem);
struct instrument_state final
{
  BOOST_PP_SEQ_FOR_EACH(DECLARE_FIELD, _, FEED_FIELDS)
  std::bitset<BOOST_PP_SEQ_SIZE(FEED_FIELDS)> updates;
  sequence_id_type sequence_id;
};
#undef DECLARE_FIELD

HOTPATH inline void update_state(instrument_state &state, const message &message) noexcept
{
  for(auto &&update: message.updates)
  {
    switch(update.field)
    {
      // clang-format off
#define HANDLE_FIELD(r, _, elem) \
    case BOOST_PP_TUPLE_ELEM(1, elem): \
      state.BOOST_PP_TUPLE_ELEM(0, elem) = value_proxy<const struct update, BOOST_PP_TUPLE_ELEM(2, elem)>(boilerplate::make_strict_not_null(&update)); \
      state.updates.set(static_cast<std::size_t>(field_index::BOOST_PP_TUPLE_ELEM(0, elem))); \
      break;
    BOOST_PP_SEQ_FOR_EACH(HANDLE_FIELD, _, FEED_FIELDS)
#undef HANDLE_FIELD
      // clang-format on
    }
  }
}

template<typename continuation_type>
HOTPATH void visit_state(continuation_type &&continuation, const instrument_state &state) noexcept
{
  // clang-format off
#define HANDLE_FIELD(r, _, elem) \
  continuation(BOOST_PP_CAT(BOOST_PP_TUPLE_ELEM(0, elem), _c){}, state.BOOST_PP_TUPLE_ELEM(0, elem));
  BOOST_PP_SEQ_FOR_EACH(HANDLE_FIELD, _, FEED_FIELDS)
#undef HANDLE_FIELD
  // clang-format on
}

auto decode_message(const asio::const_buffer &buffer) noexcept
{
  namespace x3 = boost::spirit::x3;

  static const struct fields_ : x3::symbols<std::uint8_t>
  {
    fields_()
    {
      // clang-format off
#define HANDLE_FIELD(r, _, elem) (BOOST_PP_STRINGIZE(BOOST_PP_TUPLE_ELEM(0, elem)), BOOST_PP_TUPLE_ELEM(1, elem))
      add BOOST_PP_SEQ_FOR_EACH(HANDLE_FIELD, _, FEED_FIELDS);
#undef HANDLE_FIELD
      // clang-format on
    }
  } field_symbol;

  static const auto update_rule = x3::rule<class update_class, update>("update") = field_symbol >> ':' >> (x3::float_ | x3::int_);
  static const auto message_rule = x3::rule<class message_class, message>("message")
    = x3::lit("inst") >> ':' >> x3::int_ >> ';' >> "seq" >> ':' >> x3::int_ >> ';' >> (update_rule % ';') >> '\n';
  static const auto timestamp_rule = x3::rule<class timestamp_class, clock::time_point>("timestamp")
    = x3::lit("ts") >> ':' >> x3::int_[([](auto &context) { x3::_val(context) = clock::time_point(clock::duration(x3::_attr(context))); })];
  static const auto grammar = timestamp_rule >> ';' >> message_rule;

  std::tuple<clock::time_point, message> result;
  x3::phrase_parse(reinterpret_cast<const char *>(buffer.data()), reinterpret_cast<const char *>(buffer.data()) + buffer.size(), grammar, x3::space, result);
  return result;
}

struct snapshot_request_coroutine final
{
  asio::ip::tcp::socket &socket;

  func::function<void(instrument_state &)> termination_handler;

  asio::coroutine coroutine {};
  std::string buffer;

  snapshot_request_coroutine(asio::ip::tcp::socket &socket, instrument_id_type instrument,
                             const func::function<void(instrument_state &)> &termination_handler) noexcept:
    socket(socket),
    termination_handler(termination_handler), buffer(fmt::format("inst: {:04x}", instrument))
  {
  }

  void operator()() noexcept
  {
    ASIO_CORO_REENTER(coroutine)
    {
      ASIO_CORO_YIELD asio::async_write(socket, asio::buffer(buffer), [&](auto error_code, [[maybe_unused]] auto bytes_transferred) {
        if(error_code) [[unlikely]]
        {
          BOOST_LEAF_NEW_ERROR(error_code);
          return;
        }
        (*this)();
      });

      ASIO_CORO_YIELD asio::async_read_until(socket, asio::dynamic_buffer(buffer), "\n\n", [&](auto error_code, auto bytes_transferred) {
        if(error_code) [[unlikely]]
        {
          BOOST_LEAF_NEW_ERROR(error_code);
          return;
        }
        const auto [timestamp, message] = decode_message(asio::buffer(buffer, bytes_transferred));
        instrument_state state {.sequence_id = message.sequence_id};
        update_state(state, message);

        termination_handler(state);
      });
    }
  }
};

auto request_snapshot(asio::ip::tcp::socket &socket, instrument_id_type instrument, const std::function<void(instrument_state &)> &termination_handler) noexcept
{
  return snapshot_request_coroutine(socket, instrument, termination_handler);
}

template<typename message_header_handler_type, typename update_handler_type>
HOTPATH void decode(message_header_handler_type &&message_header_handler, update_handler_type &&update_handler, const clock::time_point &,
                    asio::const_buffer &&buffer) noexcept
{
  const auto [timestamp, message] = decode_message(buffer);
  const auto instrument_closure = message_header_handler(message.instrument, message.sequence_id);
  if(!instrument_closure) [[unlikely]]
    return;

  auto timestamp_ = timestamp; // TODO: only to silence clang
  std::for_each(message.updates.begin(), message.updates.end(), [&](auto &update) { update_handler(timestamp_, update, instrument_closure); });
}

} // namespace detail

using detail::update;
using detail::visit_state;
using detail::visit_update;

constexpr auto decode = hof::lazy(BOOST_HOF_LIFT(detail::decode));

using detail::instrument_state;
using detail::request_snapshot;
using detail::snapshot_request_coroutine;

} // namespace feed

template<feed::field field, typename char_type>
struct fmt::formatter<feed::field_c<field>, char_type> : fmt::formatter<const char *, char_type>
{
  template<typename context_type>
  auto format(feed::field_c<field> value, context_type &context)
  {
// clang-format off
#define HANDLE_FIELD(r, _, elem) if constexpr(value() == feed::field::BOOST_PP_TUPLE_ELEM(0, elem)) return format_to(context.out(), BOOST_PP_STRINGIZE(BOOST_PP_TUPLE_ELEM(0, elem)));
    BOOST_PP_SEQ_FOR_EACH(HANDLE_FIELD, _, FEED_FIELDS)
#undef HANDLE_FIELD
    // clang-format on
  }
};

#if defined(DOCTEST_LIBRARY_INCLUDED)

TEST_SUITE("feed_text")
{
  TEST_CASE("decode")
  {
    using namespace std::string_literals;

    const auto [timestamp, message] = feed::detail::decode_message(asio::buffer("ts: 24; inst: 7; seq: 12; o0: 2.5; bq0: 4;\n"s));
    CHECK(timestamp.time_since_epoch().count() == 24);
    CHECK(message.instrument == 7);
    CHECK(message.sequence_id == 12);

    CHECK(message.updates.size() == 2);
    CHECK(feed::visit_update<bool>(
      [](auto field, auto value) {
        if constexpr(field() == feed::field::o0)
          return value == feed::price_t(2.5);
        else
          return false;
      },
      message.updates[0]));
    CHECK(feed::visit_update<bool>(
      [](auto field, auto value) {
        if constexpr(field() == feed::field::bq0)
          return value == feed::quantity_t(4);
        else
          return false;
      },
      message.updates[1]));
  }
}

#endif // defined(DOCTEST_LIBRARY_INCLUDED)
