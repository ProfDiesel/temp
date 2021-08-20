#pragma once

#include <simdjson.hpp>
#include <boost/sml.hpp>
#include <boost/te.hpp>

namespace te = boost::te;

using instrument_id_type = std::byte[4];
using order_id_type = std::byte[16];
using address_type = asio::ip::address;
using version_type = std::byte[4];

using byte_array = std::vector<byte>;


struct Message: te::poly<Message> {
  using te::poly<Message>::poly;

  void marshall(asio::buffer &buffer) const {
    te::call([](auto const &self, auto &buffer) { self.marshall(buffer); }, *this, buffer);
  }

  void unmarshall(asio::buffer &buffer) {
    te::call([](auto const &self, auto &buffer) { self.unmarshall(buffer); }, *this, buffer);
  }
};

void marshall(const Message &message, asio::buffer &buffer) { message.marshall(buffer); }


struct handshake
{
  constexpr auto TYPE = "handshake";

	version_type version;
  std::vector<byte> credentials;
  asio::ip::address datagram_address;
};

struct handshake_response
{
  constexpr auto TYPE = "handshake_response";

  bool ok;
  std::string message;
};

struct message
{
  constexpr auto TYPE = "message";

  instrument_id_type instrument;
  std::vector<byte> payload;
};

struct new_order
{
  constexpr auto TYPE = "new_order";

  instrument_id_type instrument;
  quantity_type quantity;
  price_type price;
};

struct cancel_order
{
  constexpr auto TYPE = "cancel_order";

  order_id_type id;
};

struct order_status
{
  constexpr auto TYPE = "order_status";

  order_id_type id;
  order_status_type status;
  std::string message;
};

using client_messages = h::tuple<handshake, new_order, cancel_order>;
using server_messages = h::tuple<handshake_response, order_status>;

using map_of_types = h::make_map();

struct session_fsm
{
  auto operator()() const
  {
    using namespace sml;
    return make_transition_table(
        *"new"_s + event<handshake_response>[is_ok] / "connected"_s
        );
  }
};

struct order_sm
{
  auto operator()() const
  {
    using namespace sml;
    return make_transition_table(
        *"new"_s + event<order_status>[is_on_market] / "on_market"_s,
        *"on_market"_s + event<order_status>[is_cancelled] / "cancelled"_s,
        );
  }
};

boost::leaf::awaitable<boost::leaf::result<void>> loop(auto continuation, asio::tcp::socket &socket)
{
  const auto message_id = BOOST_LEAF_CO_TRYX(read<message_id_type>(socket));


  const auto args = h::transform(h::keys<message_type>(), [&json](auto &&key) { return json[key]; });
}

boost::leaf::awaitable<boost::leaf::result<void>> pipo()
{
  auto multiplex = overload {
    [](handshake_response &&message) {
    },
    [](order_status &&message) {
    },
  };

  loop(multiplex, socket);
}

struct decimal_float
{
  int sign_: 1;
  int exponent_: 4; // complement à 2
  int mantissa_: 27;

  static constexpr auto MAX_EXPONENT;
  static constexpr auto MAX_MANTISSA;

  decimal_float(bool sign, std::uint8_t exponent, std::uint32_t mantissa): sign_(sign), exponent_(exponent), mantissa_(mantissa) {}

  decimal_float(auto value) requires std::is_arithmetic_v<decltype(value)>
  {
    if constexpr(std::is_floating_point_v<decltype(value)>)
    {
      const bool sign = std::signbit(value);
      int exponent = 0;
      auto mantissa = std::frexp(value, &exponent);
    }
    else if constexpr(std::is_integral_v<decltype(value)>)
    {
    }
  }

  auto sign() { return UNLIKELY(sign_) ? -1 : 1; }

  auto factor() { return std::pow(10, exponent); }

  auto as_float() { return mantissa * factor() * sign(); }
};
