#include "feed/feed_binary.hpp"
#include "feed/feed_server.hpp"

#include "fuzz_test_harness.hpp"

#include <array>
#include <unistd.h>

int main(int argc, char *argv[])
{
  feed::state_map state_map;

  auto flush = [&](auto time_offset, auto instrument, auto state)
  {
    static std::array<std::byte, fuzz::feeder::input_buffer_size> garbage;

    state_map.update(std::vector {std::tuple {instrument, state}},
                     [&](auto &&buffer)
                     {
                       assert(buffer.size() <= fuzz::feeder::input_buffer_size);
                       const auto *packet = reinterpret_cast<const feed::detail::packet *>(buffer.data());
                       const auto advance = [](const feed::message *&message)
                       {
                         // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast, cppcoreguidelines-pro-bounds-pointer-arithmetic)
                         message = reinterpret_cast<const feed::message *>(reinterpret_cast<const std::byte *>(message) + sizeof(*message)
                                                                            + message->nb_updates * sizeof(feed::update));
                       };
                       for(auto [i, message] = std::tuple {0, &packet->message}; i < packet->nb_messages; ++i, advance(message))
                       {
                         ::write(STDOUT_FILENO, &time_offset, sizeof(time_offset));
                         ::write(STDOUT_FILENO, buffer.data(), buffer.size());
                         ::write(STDOUT_FILENO, garbage.data(), fuzz::feeder::input_buffer_size - buffer.size());
                         ::fsync(STDOUT_FILENO);
                       }
                     });
  };

  feed::instrument_id_type instrument_42 {42};
  feed::instrument_state state_42;

  feed::update_state(state_42, feed::b0_v, 9);
  feed::update_state(state_42, feed::bq0_v, 1);
  feed::update_state(state_42, feed::o0_v, 10);
  feed::update_state(state_42, feed::oq0_v, 1);
  flush(0, instrument_42, state_42);

  feed::update_state(state_42, feed::b0_v, 8);
  feed::update_state(state_42, feed::bq0_v, 51);
  feed::update_state(state_42, feed::o0_v, 10);
  feed::update_state(state_42, feed::oq0_v, 1);
  flush(10, instrument_42, state_42);

  return 0;
}
