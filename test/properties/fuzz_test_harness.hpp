#pragma once

#include "feed/feed.hpp"

#include <asio/buffer.hpp>

#include <boost/leaf/result.hpp>

#include <limits>
#include <random>
#include <type_traits>

void bad_test() { ::exit(0); }
void failed_test() { ::abort(); }

namespace fuzz
{
struct fd_generator
{
  using result_type = unsigned int;

  constexpr result_type min() const noexcept { return std::numeric_limits<result_type>::min(); }
  constexpr result_type max() const noexcept { return std::numeric_limits<result_type>::max(); }

  int fd {};

  auto operator()() noexcept
  {
    result_type result;
    if(::read(fd, &result, sizeof(result)) != sizeof(result))
      bad_test();
    return result;
  }
};

struct update_source
{
  std::aligned_storage_t<65'536, alignof(feed::details::packet)> buffer_ {};
  fd_generator gen {.fd = STDIN_FILENO}; // TODO default to ::open('/dev/urandom') ?

  clock::time_point timestamp {};

  std::vector<feed::instrument_state> states;

  template<typename continuation_type>
  [[using gnu : always_inline, flatten, hot]] auto operator()(continuation_type &continuation) noexcept
    -> leaf::result<typename std::invoke_result_t<continuation_type, clock::time_point, asio::const_buffer &&>::value_type>
  {
    auto random_duration = [&]() {
      std::uniform_int_distribution<clock::rep> distribution(0, 1'000'000'000);
      return clock::duration {distribution(gen)};
    };

    auto random_size = [&](std::size_t max) {
      std::uniform_int_distribution<std::size_t> distribution(0, max);
      return distribution(gen);
    };

    auto random_price = [&]() {
      constexpr auto min_price = 90.0;
      constexpr auto max_price = 200.0;
      std::uniform_real_distribution<float> distribution(min_price, max_price);
      return feed::price_t {std::decimal::decimal32 {distribution(gen)}};
    };

    auto random_quantity = [&]() {
      std::uniform_int_distribution<std::size_t> distribution;
      return distribution(gen);
    };

    timestamp += random_duration();

    auto result = new(&buffer_) feed::details::packet {.nb_messages = random_size(states.size())};
    for(auto &&[message_index, message_addr] = {0, &result->message}; message_index < result->nb_messages; ++message_index)
    {
      auto instrument = random_size(states.size());
      auto nb_updates = random_size(5);
      auto &state = states[instrument];

      for(auto update_index = 0, nb_updates = random_size(5); update_index < nb_updates; ++update_index)
      {
        const std::array functions {std::function {[&]() { return feed::details::update_state(state, feed::b0_c {}, random_price()); }},
                                    std::function {[&]() { return feed::details::update_state(state, feed::bq0_c {}, random_quantity()); }},
                                    std::function {[&]() { return feed::details::update_state(state, feed::o0_c {}, random_price()); }},
                                    std::function {[&]() { return feed::details::update_state(state, feed::oq0_c {}, random_quantity()); }}};
        std::uniform_int_distribution<std::size_t> distribution(0, functions.size());
        functions[distribution(gen)]();
      }

      message_addr = encode_message(instrument, state, message_addr);
    }
    generate_packet();
    bool triggered = continuation(timestamp, asio::const_buffer(buffer_));
    return triggered;
  }
};

struct stream_send
{
  auto operator()(const asio::const_buffer &buffer) noexcept {}
};
} // namespace fuzz

#define BACKTEST_HARNESS

namespace backtest
{
auto make_update_source() { return fuzz::update_source {}; }
auto make_stream_send() { return fuzz::stream_send {}; }

} // namespace backtest

namespace invariant
{
// triggers
// jamais dans le buffer de situation de trigger (ou automaton en cooldown)

// cooldown doit etre résistant aux souscriptions dynamiques
} // namespace invariant
