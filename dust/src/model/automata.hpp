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
#include <boost/core/noncopyable.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <type_traits>
#include <unistd.h>

namespace bco = boost::container;
namespace b = boilerplate;

template<bool handle_packet_loss_, typename trigger_type_, bool send_datagram_>
struct automaton final
{
  static constexpr auto handle_packet_loss = handle_packet_loss_;
  using trigger_type = trigger_type_;
  static constexpr auto send_datagram = send_datagram_;

  trigger_type trigger;
  payload_type payload;

  /*const*/
  feed::instrument_id_type instrument_id = {};

  [[no_unique_address]] std::conditional_t<handle_packet_loss, feed::sequence_id_type, b::empty> sequence_id = {};
  [[no_unique_address]] std::conditional_t<handle_packet_loss, bool, b::empty> snapshot_request_running;

  bool handle_sequence_id(feed::sequence_id_type sequence_id, auto snapshot_requester)
  {
    if (handle_packet_loss || UNLIKELY(this->sequence_id == feed::sequence_id_type {}))
    {
      const auto diff = sequence_id - (this->sequence_id + 1);
      if(!diff) [[likely]]
        return true;
      if(diff < 0) [[unlikely]]
        return false;
      return request_snapshot();
    }

    return true;
  }

  bool request_snapshot() noexcept
  {
    if(std::exchange(snapshot_request_running, true)) [[unlikely]]
      return false;
    snapshot_requester(instrument_id, [this]() { if constexpr(handle_packet_loss) snapshot_request_running = false; });
    return true;
  }

  void apply(feed::instrument_state &&state) noexcept {
    trigger.reset(std::move(state));
    if constexpr(handle_packet_loss) this->sequence_id = sequence_id;
  };

  operator const payload_type &() const noexcept { return payload; }
};

template<typename automaton_type, bool dynamic_subscription_>
struct automata final /*: boost::noncopyable*/
{
  static constexpr auto dynamic_subscription = dynamic_subscription_;

  boilerplate::not_null_observer_ptr<logger::logger> logger_ptr;

  template<typename value_type>
  using sequence = std::conditional_t<dynamic_subscription, bco::small_vector<value_type, std::hardware_destructive_interference_size / sizeof(value_type)>,
                                      std::array<value_type, 1>>;

  sequence<feed::instrument_id_type> instrument_ids;
  sequence<automaton> data;

  static constexpr feed::instrument_id_type INVALID_INSTRUMENT = 0;

  explicit automata(boilerplate::not_null_observer_ptr<logger::logger> logger_ptr) noexcept requires dynamic_subscription : logger_ptr(logger_ptr) {}
  automata(boilerplate::not_null_observer_ptr<logger::logger> logger_ptr, automaton &&automaton) noexcept requires(!dynamic_subscription):
    logger_ptr(logger_ptr), instrument_ids {{INVALID_INSTRUMENT}}, data {{std::move(automaton)}}
  {
  }

  automata(automata &&) noexcept = default;

  [[using gnu: always_inline, flatten, hot]] inline automaton *at_if_not_disabled(feed::instrument_id_type instrument_id) noexcept
  {
    const auto it = std::find(instrument_ids.begin(), instrument_ids.end(), instrument_id);
    return LIKELY(it != instrument_ids.end()) ? &data[it - instrument_ids.begin()] : nullptr;
  }

  automaton *at(feed::instrument_id_type instrument_id) noexcept
  {
    const auto it = std::find_if(data.begin(), data.end(), [&](const auto &automaton) { return automaton.instrument_id == instrument_id; });
    return it != data.end() ? &data[std::size_t(it - data.begin())] : nullptr;
  }

  void emplace(feed::instrument_id_type instrument_id, automaton &&automaton) noexcept requires dynamic_subscription
  {
    if(at(instrument_id)) [[unlikely]]
      return;
    data.push_back(std::move(automaton));
    instrument_ids.push_back(instrument_id);
  }

  void erase(feed::instrument_id_type instrument_id) noexcept requires dynamic_subscription
  {
    if(const auto *automaton = at(instrument_id); automaton) [[likely]]
    {
      instrument_ids.erase(instrument_ids.begin() + (automaton - &data[0]));
      data.erase(data.begin() + (automaton - &data[0]));
    }
  };

  auto enter_cooldown(automaton *instrument) noexcept
  {
    REQUIRES(instrument);
    instrument_ids[std::size_t(instrument - &data[0])] = INVALID_INSTRUMENT;
    return [&, instrument_id = instrument->instrument_id]() { // capture the id, some subscriptions/unsubscriptions may have happened in the interval
      if(const auto *automaton = at(instrument_id); automaton) [[likely]]
        instrument_ids[std::size_t(automaton - &data[0])] = instrument_id;
    };
  }

  void each(auto continuation) noexcept
  {
    for(std::size_t i = 0; i < instrument_ids.size(); ++i)
      if(instrument_ids[i])
        continuation(&data[i]);
  }
};


[[using gnu: flatten]] auto with_automata(const config::walker &config, asio::io_context &service, 
                       boilerplate::not_null_observer_ptr<logger::logger> logger_ptr, auto continuation) noexcept
{
  using namespace config::literals;

  auto wrapped = [&](auto dynamic_subscription, auto &&trigger, auto handle_packet_loss, auto send_datagram) noexcept -> boost::leaf::result<void>
  {
    const auto subscription = config["subscription"_hs];

    using automaton_type = automaton<handle_packet_loss(), trigger_type(), send_datagram()>;
    using automata_type = automata<automaton_type, dynamic_subscription()>;

    auto automata = BOOST_LEAF_TRYX(
      [&]() noexcept -> boost::leaf::result<automata_type>
      {
        if constexpr(!dynamic_subscription())
        {
          const auto instrument_id = subscription["trigger"_hs]["instrument"_hs];
          auto payload = BOOST_LEAF_TRYX(decode_payload<send_datagram()>(subscription["payload"_hs]));
          auto automaton = typename automata_type::automaton {.trigger = std::move(trigger),
                                                              .payload = std::move(payload),
                                                              .instrument_id = instrument_id};
          return automata_type(logger_ptr, std::move(automaton));
        }
        else
          return automata_type(logger_ptr);
      }());

      return continuation(std::move(automata));
  };

#if !defined(LEAN_AND_MEAN) && !defined(FUZZ_TEST_HARNESS)
  const auto dynamic_subscription_test = [&](auto continuation, auto &&...tags) noexcept
  {
    const auto trigger = config["subscription"_hs]["trigger"_hs];
    return trigger ? with_trigger(trigger, logger_ptr,
                                       [&](auto &&trigger_dispatcher)
                                       { return continuation(std::forward<decltype(tags)>(tags)..., std::false_type {}, std::move(trigger_dispatcher)); })()
                        : continuation(std::forward<decltype(tags)>(tags)..., std::true_type {}, polymorphic_trigger_dispatcher());
  };

  const auto handle_packet_loss_test = [&](auto continuation, auto &&...tags) noexcept
  {
    return config["feed"_hs]["handle_packet_loss"_hs] ? continuation(std::forward<decltype(tags)>(tags)..., std::true_type {})
                                                      : continuation(std::forward<decltype(tags)>(tags)..., std::false_type {});
  };

  const auto send_datagram_test = [&](auto continuation, auto &&...tags) noexcept
  {
    return config["send"_hs]["datagram"_hs] ? continuation(std::forward<decltype(tags)>(tags)..., std::true_type {})
                                            : continuation(std::forward<decltype(tags)>(tags)..., std::false_type {});
  };

  using namespace piped_continuation;
  return dynamic_subscription_test |= handle_packet_loss_test |= send_datagram_test
         |= hof::partial(wrapped)(service, logger_ptr, continuation, properties);
#else  // !defined(LEAN_AND_MEAN) && !defined(FUZZ_TEST_HARNESS)
  const auto trigger = config["subscription"_hs]["trigger"_hs];
  return with_trigger(trigger, logger_ptr,
                      [&](auto &&trigger_dispatcher)
                      {
                        return wrapped(config, service, logger_ptr, continuation, std::false_type {},
                                   std::forward<decltype(trigger_dispatcher)>(trigger_dispatcher), std::false_type {}, std::false_type {});
                      });
#endif // !defined(LEAN_AND_MEAN) && !defined(FUZZ_TEST_HARNESS)
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
