#pragma once

#include "payload.hpp"

#include <boilerplate/logger.hpp>
#include <boilerplate/pointers.hpp>

#include <feed/feed.hpp>

#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>

#include <boost/container/small_vector.hpp>
#include <boost/core/noncopyable.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <type_traits>
#include <unistd.h>

namespace bco = boost::container;
namespace b = boilerplate;

template<bool dynamic_subscription_, bool handle_packet_loss_, typename trigger_type_, bool send_datagram_>
struct automata final /*: boost::noncopyable*/
{
  static constexpr auto dynamic_subscription = dynamic_subscription_;
  static constexpr auto handle_packet_loss = handle_packet_loss_;
  using trigger_type = trigger_type_;
  static constexpr auto send_datagram = send_datagram_;

  using payload_type = payload<send_datagram>;

  struct automaton final
  {
    trigger_type trigger;
    payload_type payload;

    /*const*/
    feed::instrument_id_type instrument_id = {};

    [[no_unique_address]] std::conditional_t<handle_packet_loss, feed::sequence_id_type, b::empty> sequence_id = {};
    [[no_unique_address]] std::conditional_t<handle_packet_loss, bool, b::empty> snapshot_request_running;

    operator const payload_type &() const noexcept { return payload; }
  };

  boilerplate::not_null_observer_ptr<logger::logger> logger;

  template<typename value_type>
  using sequence = std::conditional_t<dynamic_subscription, bco::small_vector<value_type, std::hardware_destructive_interference_size / sizeof(value_type)>,
                                      std::array<value_type, 1>>;

  sequence<feed::instrument_id_type> instrument_ids;
  sequence<automaton> data;

  static constexpr feed::instrument_id_type INVALID_INSTRUMENT = 0;

  explicit automata(boilerplate::not_null_observer_ptr<logger::logger> logger) noexcept requires dynamic_subscription : logger(logger) {}
  automata(boilerplate::not_null_observer_ptr<logger::logger> logger, automaton &&automaton) noexcept requires(!dynamic_subscription):
    logger(logger), instrument_ids {{INVALID_INSTRUMENT}}, data {{std::move(automaton)}}
  {
  }

  automata(automata &&) noexcept = default;

  [[using gnu: always_inline, flatten, hot]] inline automaton *instrument(feed::instrument_id_type instrument_id) noexcept
  {
    const auto it = std::find(instrument_ids.begin(), instrument_ids.end(), instrument_id);
    return LIKELY(it != instrument_ids.end()) ? &data[it - instrument_ids.begin()] : nullptr;
  }

  automaton *instrument_maybe_disabled(feed::instrument_id_type instrument_id) noexcept
  {
    const auto it = std::find_if(data.begin(), data.end(), [&](const auto &automaton) { return automaton.instrument_id == instrument_id; });
    return it != data.end() ? &data[std::size_t(it - data.begin())] : nullptr;
  }

  void track(feed::instrument_id_type instrument_id, automaton &&automaton) noexcept requires dynamic_subscription
  {
    if(instrument_maybe_disabled(instrument_id)) [[unlikely]]
      return;
    data.push_back(std::move(automaton));
    instrument_ids.push_back(instrument_id);
  }

  void untrack(feed::instrument_id_type instrument_id) noexcept requires dynamic_subscription
  {
    if(const auto *automaton = instrument_maybe_disabled(instrument_id); automaton) [[likely]]
    {
      instrument_ids.erase(instrument_ids.begin() + (automaton - &data[0]));
      data.erase(data.begin() + (automaton - &data[0]));
    }
  };

  void update_payload(feed::instrument_id_type instrument_id, payload_type &&payload) noexcept
  {
    if(auto *automaton = instrument_maybe_disabled(instrument_id); automaton) [[likely]]
      automaton->payload = std::move(payload);
  }

  auto enter_cooldown(automaton *instrument) noexcept
  {
    REQUIRES(instrument);
    instrument_ids[std::size_t(instrument - &data[0])] = INVALID_INSTRUMENT;
    return [&, instrument_id = instrument->instrument_id]() { // capture the id, some subscriptions/unsubscriptions may have happened in the interval
      if(const auto *automaton = instrument_maybe_disabled(instrument_id); automaton) [[likely]]
        instrument_ids[std::size_t(automaton - &data[0])] = instrument_id;
    };
  }

  void warm_up(auto &&continuation) noexcept
  {
    for(std::size_t i = 0; i < instrument_ids.size(); ++i)
      if(instrument_ids[i])
      {
        data[i].trigger.warm_up();
        continuation(&data[i]);
      }
  }
};

#if defined(DOCTEST_LIBRARY_INCLUDED)

struct dummy_trigger_type
{
};

template<bool dynamic_subscription>
using test_automata = automata<dynamic_subscription, false, dummy_trigger_type, false>;

TEST_SUITE("automata")
{
  /*
  TEST_CASE("subscription")
  {
    automaton a0;
    test_automata<true> a;
    feed::instrument_id_type instrument;
    CHECK(a.instrument(instrument) == nullptr);
    a.add(instrument, automaton);
    CHECK(a.instrument(instrument) != nullptr);
    a.remove(instrument);
    CHECK(a.instrument(instrument) = nullptr);
  }

  TEST_CASE_TEMPLATE("cooldown", T, automata<true>, automata<false>)
  {
    automaton a0;
    T a = []() {
      if constexpr(T::dynamic_subscription)
      {
        T a {logger};
        a.add(a0);
      }
      else
      {
        return T {logger, a0};
      }
    }();
    feed::instrument_id_type instrument;
    a.add(instrument, automaton);
    CHECK(a.instrument(instrument) != nullptr);
    a.remove(instrument);
    CHECK(a.instrument(instrument) = nullptr);
  }
  */
}
#endif // defined(DOCTEST_LIBRARY_INCLUDED)
