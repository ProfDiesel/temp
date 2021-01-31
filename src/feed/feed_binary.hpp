#pragma once

#include "feed_structures.hpp"

#include <boilerplate/boilerplate.hpp>
#include <boilerplate/chrono.hpp>
#include <boilerplate/outcome.hpp>

#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#include <boost/endian/conversion.hpp>

#include <boost/hof/lazy.hpp>
#include <boost/hof/lift.hpp>

#include <std_function/function.h>

#include <array>
#include <bitset>
#include <cstdint>

namespace feed
{
namespace endian = boost::endian;
namespace hof = boost::hof;

namespace detail
{
struct packet final
{
  const std::uint8_t nb_messages = 0;
  const struct message message = {};
} __attribute__((packed));
static_assert(sizeof(packet) == 13); // NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

struct snapshot_request final
{
  const endian::big_uint16_buf_t instrument {};
};
static_assert(sizeof(snapshot_request) == 2); // NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)


constexpr std::size_t message_max_size = sizeof(message) + (static_cast<int>(field_index::_count) - 1) * sizeof(update);

inline auto encode_message(instrument_id_type instrument, const instrument_state &state, const asio::mutable_buffer &buffer) noexcept
{
  using struct_update = struct update;
  using struct_message = struct message;
  const auto nb_updates = static_cast<uint8_t>(state.updates.count());
  const auto needed_bytes = sizeof(struct_message) + sizeof(update) * (nb_updates - 1);
  if(buffer.size() >= needed_bytes) 
  {
  auto *message = new(buffer.data()) struct_message {.instrument = endian::big_uint16_buf_t(instrument),
                                              .sequence_id = endian::big_uint32_buf_t(state.sequence_id),
                                              .nb_updates = nb_updates};
  visit_state([update = std::ref(message->update)]( auto field, auto value) mutable { detail::encode_update(field, value, update); }, state);
  }

  return needed_bytes;
}

inline asio::awaitable<out::result<instrument_state>> request_snapshot(asio::ip::tcp::socket &socket, instrument_id_type instrument) noexcept
{
  const snapshot_request request {.instrument = endian::big_uint16_buf_t(instrument)};
  if(OUTCOME_CO_TRYX(co_await asio::async_write(socket, asio::const_buffer(&request, sizeof(request)), as_result(asio::use_awaitable))) != sizeof(request))
    co_return out::failure(std::make_error_code(std::errc::io_error)); // TODO

  std::aligned_storage_t<message_max_size, alignof(message)> buffer;
  auto *current_message = reinterpret_cast<message *>(&buffer);
  if(OUTCOME_CO_TRYX(co_await asio::async_read(socket, asio::buffer(current_message, sizeof(message)), as_result(asio::use_awaitable))) != sizeof(message))
    co_return out::failure(std::make_error_code(std::errc::io_error)); // TODO
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  const auto remaining = (current_message->nb_updates - 1) * sizeof(update);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  if(OUTCOME_CO_TRYX(co_await asio::async_read(socket, asio::buffer(current_message + 1, remaining), as_result(asio::use_awaitable))) != remaining)
    co_return out::failure(std::make_error_code(std::errc::io_error)); // TODO

  instrument_state state {.sequence_id = current_message->sequence_id.value()};
  update_state(state, *current_message);

  co_return state;
}

template<typename message_header_handler_type, typename update_handler_type>
[[using gnu : always_inline, flatten, hot]] void decode(message_header_handler_type &&message_header_handler, update_handler_type &&update_handler, const clock::time_point &timestamp,
                    asio::const_buffer &&buffer) noexcept
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
    const auto instrument_closure = message_header_handler(message->instrument, message->sequence_id);
    if(UNLIKELY(!instrument_closure))
      continue;

    std::for_each_n(&message->update, message->nb_updates, [&](auto &update) { update_handler(timestamp, update, instrument_closure); });
  }
}

} // namespace detail

constexpr auto decode = hof::lazy(BOOST_HOF_LIFT(detail::decode));

using detail::request_snapshot;

namespace sample_packets
{

template<std::size_t size>
struct alignas(feed::detail::packet) aligned_byte_array : public std::array<std::byte, size>
{
};

template<typename... args_types>
constexpr aligned_byte_array<sizeof...(args_types)> make_packet(args_types &&...args) noexcept
{
  return {std::byte(std::forward<args_types>(args))...};
}

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
    //        [](clock::time_point timestamp, const feed::update &update, int instrument_closure){ CHECK(timestamp == 0); CHECK(instrument_closure == 0); }, 0, packet_0); 
  }
}

// GCOVR_EXCL_STOP
#endif // defined(DOCTEST_LIBRARY_INCLUDED)

