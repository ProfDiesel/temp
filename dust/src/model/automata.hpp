#pragma once

#include "../config/config_reader.hpp"
#include "../config/dispatch.hpp"
#include "../trigger/trigger_dispatcher.hpp"
#include "automata.hpp"
#include "payload.hpp"

#include <boilerplate/contracts.hpp>
#include <boilerplate/leaf.hpp>
#include <boilerplate/logger.hpp>
#include <boilerplate/piped_continuation.hpp>
#include <boilerplate/pointers.hpp>

#include <feed/feed.hpp>

#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>

#include <boost/container/small_vector.hpp>
#include <boost/hana/type.hpp>
#include <boost/hof/partial.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <type_traits>
#include <unistd.h>

namespace bco = boost::container;
namespace hof = boost::hof;
namespace b = boilerplate;


template<bool handle_packet_loss_, typename trigger_type_, bool send_datagram_>
struct automaton final
{
  static constexpr auto handle_packet_loss = handle_packet_loss_;
  using trigger_type = trigger_type_;
  static constexpr auto send_datagram = send_datagram_;

  using payload_type = payload<send_datagram>;

  trigger_type trigger;
  payload_type payload;

  /*const*/
  feed::instrument_id_type instrument_id = {};

  [[no_unique_address]] std::conditional_t<handle_packet_loss, feed::sequence_id_type, b::empty> sequence_id = {};
  [[no_unique_address]] std::conditional_t<handle_packet_loss, bool, b::empty> snapshot_request_running;

  bool handle_sequence_id(feed::sequence_id_type sequence_id, auto snapshot_requester) noexcept requires handle_packet_loss
  {
      const auto diff = sequence_id - (this->sequence_id + 1);
      if(!diff) [[likely]]
        return true;
      if(diff < 0) [[unlikely]]
        return false;
      if(std::exchange(snapshot_request_running, true)) [[unlikely]]
        return false;
      snapshot_requester(instrument_id, [this]() noexcept { snapshot_request_running = false; });
      return true;
  }

  constexpr bool handle_sequence_id(feed::sequence_id_type, auto) noexcept requires (!handle_packet_loss) { return true; }

  void apply(feed::instrument_state &&state) noexcept {
    trigger.reset(std::move(state));
    if constexpr(handle_packet_loss)
      this->sequence_id = sequence_id;
  };

  operator const payload_type &() const noexcept { return payload; }
};

template<typename automaton_type, bool dynamic_subscription_>
struct automata final
{
  static constexpr auto dynamic_subscription = dynamic_subscription_;

  template<typename value_type>
  using sequence = std::conditional_t<dynamic_subscription, bco::small_vector<value_type, std::hardware_destructive_interference_size / sizeof(value_type)>,
                                      std::array<value_type, 1>>;

  sequence<feed::instrument_id_type> instrument_ids;
  sequence<automaton_type> data;

  static constexpr feed::instrument_id_type INVALID_INSTRUMENT = 0;

  automata() noexcept requires dynamic_subscription {}
  automata() noexcept requires(!dynamic_subscription): instrument_ids {{INVALID_INSTRUMENT}} {}

  automata(const automata&) noexcept = delete;
  automata(automata &&) noexcept = default;

  automata &operator=(const automata&) noexcept = delete;
  automata &operator=(automata &&) noexcept = default;

  [[using gnu: always_inline, flatten, hot]] inline automaton_type *at_if_not_disabled(feed::instrument_id_type instrument_id) noexcept
  {
    const auto it = std::find(instrument_ids.begin(), instrument_ids.end(), instrument_id);
    return LIKELY(it != instrument_ids.end()) ? &data[it - instrument_ids.begin()] : nullptr;
  }

  automaton_type *at(feed::instrument_id_type instrument_id) noexcept
  {
    const auto it = std::find_if(data.begin(), data.end(), [&](const auto &automaton) { return automaton.instrument_id == instrument_id; });
    return it != data.end() ? &data[std::size_t(it - data.begin())] : nullptr;
  }

  [[using gnu: always_inline, flatten, hot]] inline const automaton_type *at_if_not_disabled(feed::instrument_id_type instrument_id) const noexcept { return b::const_cast_(this)->at_if_not_disabled(instrument_ids); }

  const automaton_type *at(feed::instrument_id_type instrument_id) const noexcept { return b::const_cast_(*this)->at(instrument_id); }

  void emplace(automaton_type &&automaton) noexcept requires dynamic_subscription
  {
    REQUIRES(automaton.instrument_id != INVALID_INSTRUMENT);
    if(at(automaton.instrument_id)) [[unlikely]]
      return;
    data.push_back(std::move(automaton));
    instrument_ids.push_back(automaton.instrument_id);
  }

  void emplace(automaton_type &&automaton) noexcept requires (!dynamic_subscription)
  {
    REQUIRES(instrument_ids == {INVALID_INSTRUMENT});
    REQUIRES(automaton.instrument_id != INVALID_INSTRUMENT);
    data[0] = std::move(automaton);
    instrument_ids[0] = automaton.instrument_id;
  }

  void erase(feed::instrument_id_type instrument_id) noexcept requires dynamic_subscription
  {
    if(const auto *automaton_ptr = at(instrument_id); automaton_ptr) [[likely]]
    {
      instrument_ids.erase(instrument_ids.begin() + (automaton_ptr - &data[0]));
      data.erase(data.begin() + (automaton_ptr - &data[0]));
    }
  };

  [[nodiscard]] auto enter_cooldown(automaton_type *automaton_ptr) noexcept
  {
    REQUIRES(automaton_ptr);
    instrument_ids[std::size_t(automaton_ptr - &data[0])] = INVALID_INSTRUMENT;
    return [&, instrument_id = automaton_ptr->instrument_id]() { // capture the id, some subscriptions/unsubscriptions may have happened in the interval
      if(const auto *automaton_ptr = at(instrument_id); automaton_ptr) [[likely]]
        instrument_ids[std::size_t(automaton_ptr - &data[0])] = instrument_id;
    };
  }

  void each(auto continuation) noexcept
  {
    for(std::size_t i = 0; i < instrument_ids.size(); ++i)
      if(instrument_ids[i])
        continuation(&data[i]);
  }

  void each(auto continuation) const noexcept { return b::const_cast_(*this)->each(continuation); }
};


[[using gnu: flatten]] auto with_automata(const config::walker &config, asio::io_context &service, 
                       boilerplate::not_null_observer_ptr<logger::logger> logger_ptr, auto continuation) noexcept
{
  using namespace config::literals;

  const auto handle_packet_loss_test = [&](auto continuation, auto &&...tags) noexcept
  {
    return config["feed"_hs]["handle_packet_loss"_hs] ? continuation(std::forward<decltype(tags)>(tags)..., std::true_type())
                                                      : continuation(std::forward<decltype(tags)>(tags)..., std::false_type());
  };

  const auto send_datagram_test = [&](auto continuation, auto &&...tags) noexcept
  {
    return config["send"_hs]["datagram"_hs] ? continuation(std::forward<decltype(tags)>(tags)..., std::true_type())
                                            : continuation(std::forward<decltype(tags)>(tags)..., std::false_type());
  };

  const auto with_fixed_automata = [&](auto subscription, auto handle_packet_loss, auto send_datagram) noexcept
  {
    const auto trigger = subscription["trigger"_hs];
    return with_trigger(trigger, logger_ptr, [&](auto &&trigger_dispatcher) { 
      const auto instrument_id = trigger["instrument"_hs];
      auto payload = BOOST_LEAF_TRYX(decode_payload<send_datagram()>(subscription["payload"_hs]));
      auto state = co_request_snapshot(instrument_id);
      return continuation(automata<automaton<handle_packet_loss(), std::decay_t<decltype(trigger_dispatcher)>, send_datagram()>, false>({.trigger = std::move(state), .payload = std::move(payload), .instrument_id = instrument_id}));
    });
  };

  const auto with_dynamic_automata = [&](auto handle_packet_loss, auto send_datagram) noexcept
  {
     return hof::partial(continuation)(automata<automaton<handle_packet_loss(), polymorphic_trigger_dispatcher, send_datagram()>, true>());
  }; 

  const auto with_automata_selector = [&](auto handle_packet_loss, auto send_datagram) noexcept
  {
    const auto subscription = config["subscription"_hs];
    const auto trigger = subscription["trigger"_hs];
    return trigger ? with_fixed_automata(subscription, handle_packet_loss, send_datagram)
                   : with_dynamic_automata(handle_packet_loss, send_datagram);
  };

#if defined(LEAN_AND_MEAN) && defined(FUZZ_TEST_HARNESS)
  with_automata_selector(std::true_type(), std::true_type());
#else  // defined(LEAN_AND_MEAN) && defined(FUZZ_TEST_HARNESS)
  using namespace piped_continuation;
  return handle_packet_loss_test |= send_datagram_test |= with_automata_selector;
#endif // defined(LEAN_AND_MEAN) && defined(FUZZ_TEST_HARNESS)
}


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
