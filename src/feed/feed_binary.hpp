#pragma once

#include "feed_structures.hpp"

#include <boilerplate/boilerplate.hpp>
#include <boilerplate/chrono.hpp>
#include <boilerplate/contracts.hpp>
#include <boilerplate/leaf.hpp>

#include <asio/awaitable.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#include <boost/endian/conversion.hpp>

#include <std_function/function.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <cstdint>

namespace feed
{
namespace endian = boost::endian;

namespace detail
{
struct packet final
{
  std::uint8_t nb_messages = 0;
  struct message message = {};
} __attribute__((packed));
static_assert(sizeof(packet) == 13); // NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

struct snapshot_request final
{
  endian::big_uint16_buf_t instrument {};
};
static_assert(sizeof(snapshot_request) == 2); // NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)


constexpr std::size_t packet_max_size = 65'536;
constexpr std::size_t message_max_size = sizeof(message) + (static_cast<int>(field_index::_count) - 1) * sizeof(update);

inline auto encode_message(instrument_id_type instrument, const instrument_state &state, const asio::mutable_buffer &buffer) noexcept
{
  const auto nb_updates = static_cast<uint8_t>(state.updates.count());
  const auto needed_bytes = sizeof(message) + sizeof(update) * (nb_updates - 1);
  if(buffer.size() >= needed_bytes)
  {
  auto *message = new(buffer.data()) (struct message) {.instrument = endian::big_uint16_buf_t(instrument),
                                              .sequence_id = endian::big_uint32_buf_t(state.sequence_id),
                                              .nb_updates = nb_updates};
  visit_state([&update = message->update]( auto field, auto value) mutable { update = encode_update(field, value); }, state);
  }

  return needed_bytes;
}

inline boost::leaf::awaitable<boost::leaf::result<instrument_state>> request_snapshot(asio::ip::tcp::socket &socket, instrument_id_type instrument) noexcept
{
  const snapshot_request request {.instrument = endian::big_uint16_buf_t(instrument)};
  if(BOOST_LEAF_CO_TRYX(co_await asio::async_write(socket, asio::const_buffer(&request, sizeof(request)), boost::leaf::as_result(boost::leaf::use_awaitable))) != sizeof(request))
    co_return std::make_error_code(std::errc::io_error); // TODO

  std::aligned_storage_t<message_max_size, alignof(struct message)> buffer;
  auto *const message = reinterpret_cast<struct message *>(&buffer);
  if(BOOST_LEAF_CO_TRYX(co_await asio::async_read(socket, asio::buffer(message, sizeof(struct message)), boost::leaf::as_result(boost::leaf::use_awaitable))) != sizeof(struct message))
    co_return std::make_error_code(std::errc::io_error); // TODO
  const auto remaining = (message->nb_updates - 1) * sizeof(update); // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  if(BOOST_LEAF_CO_TRYX(co_await asio::async_read(socket, asio::buffer(message + 1, remaining), boost::leaf::as_result(boost::leaf::use_awaitable))) != remaining) // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    co_return std::make_error_code(std::errc::io_error); // TODO

  instrument_state state {.sequence_id = message->sequence_id.value()};
  update_state(state, *message);

  co_return state;
}

[[using gnu : always_inline, flatten, hot]] void decode(auto &&message_header_handler, auto &&update_handler, const network_clock::time_point &timestamp,
                    const asio::const_buffer &buffer) noexcept
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast, cppcoreguidelines-pro-bounds-pointer-arithmetic)
  const auto *const buffer_end = reinterpret_cast<const std::byte *>(buffer.data()) + buffer.size();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const auto *packet = reinterpret_cast<const struct packet *>(buffer.data());
  const auto advance = [](const message *&message) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast, cppcoreguidelines-pro-bounds-pointer-arithmetic)
    message = reinterpret_cast<const struct message *>(reinterpret_cast<const std::byte *>(message) + sizeof(*message)
                                                         + message->nb_updates * sizeof(update));
  };
  for(auto [i, message] = std::tuple {0, &packet->message}; i < packet->nb_messages; ++i, advance(message))
  {
#if defined(SAFE_LQ2)
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    if(reinterpret_cast<const std::byte *>(message) > buffer_end)
      break;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast, cppcoreguidelines-pro-bounds-pointer-arithmetic)
    if(reinterpret_cast<const std::byte *>(&message->update + message->nb_updates) > buffer_end)
      break;
#endif // defined(SAFE_LQ2)
    const auto instrument_closure = message_header_handler(message->instrument.value(), message->sequence_id.value());
    if(UNLIKELY(!instrument_closure))
      continue;

    std::for_each_n(&message->update, message->nb_updates, [&](auto &update) { update_handler(timestamp, update, instrument_closure); });
  }
}

std::size_t sanitize(auto &&value_sanitizer, const asio::mutable_buffer &buffer) noexcept
{
  if(buffer.size() < sizeof(packet))
    return 0;

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast, cppcoreguidelines-pro-bounds-pointer-arithmetic)
  auto *const buffer_begin = reinterpret_cast<std::byte *>(buffer.data()),
       *const buffer_end = buffer_begin + buffer.size();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto *const packet = reinterpret_cast<struct packet *>(buffer_begin);
  const auto advance = [](message *&message)
  {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast, cppcoreguidelines-pro-bounds-pointer-arithmetic)
    return message = reinterpret_cast<struct message *>(reinterpret_cast<std::byte *>(message) + sizeof(message) + message->nb_updates * sizeof(update));
  };

  const std::byte *current = nullptr;
  for(auto [i, message] = std::tuple {0, &packet->message}; i < packet->nb_messages; ++i, message = advance(message))
  {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    if(current = reinterpret_cast<std::byte *>(message); current > buffer_end)
    {
      packet->nb_messages = i;
      return current - buffer_begin;
    }

    for(auto [j, update] = std::tuple {0, &message->update}; j < message->nb_updates; ++j, ++update)
    {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      if(current = reinterpret_cast<std::byte *>(update); current > buffer_end)
      {
        message->nb_updates = j;
        return current - buffer_begin;
      }

      if(std::find(all_fields.begin(), all_fields.end(), static_cast<feed::field>(update->field)) == all_fields.end())
        update->field = static_cast<std::uint8_t>(all_fields[update->field % all_fields.size()]);
      *update = visit_update([&](auto field, auto value) { return encode_update(field, value_sanitizer(field, value)); }, *update);
    }
  }

  return current - buffer_begin;
}


} // namespace detail

using detail::decode;
using detail::request_snapshot;

namespace sample_packets
{

template<std::size_t size>
struct alignas(feed::detail::packet) aligned_byte_array : public std::array<std::byte, size>
{
};

constexpr auto make_packet(auto &&...args) noexcept -> aligned_byte_array<sizeof...(args)> { return {std::byte(std::forward<decltype(args)>(args))...}; }

constexpr auto packet_0 = make_packet(0x01,                   // packet.nb_messages
                                      0x00, 0x01,             // message0.instrument
                                      0x00, 0x00, 0x00, 0x00, // message0.sequence_id
                                      0x02,                   // message0.nb_updates
                                      0x01,                   // update0_0.field
                                      0x00, 0x00, 0x00, 0x00, // update0_0.value
                                      0x01,                   // update0_1.field
                                      0x00, 0x00, 0x00, 0x00  // update0_1.value
);
} // sample_packets

} // namespace feed

#if defined(DOCTEST_LIBRARY_INCLUDED)
// GCOVR_EXCL_START

TEST_SUITE("feed_binary")
{
  TEST_CASE("decode")
  {
    //  decode([](feed:instrument_instrument_id_type instrument, feed::sequence_id_type sequence_id){ CHECK(instrument == 1); CHECK(sequence_id == 0); return 0;},
    //        [](network_clock::time_point timestamp, const feed::update &update, int instrument_closure){ CHECK(timestamp == 0); CHECK(instrument_closure == 0); }, 0, packet_0);
  }
}

// GCOVR_EXCL_STOP
#endif // defined(DOCTEST_LIBRARY_INCLUDED)
