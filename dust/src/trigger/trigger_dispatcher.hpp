#pragma once

#include "../config/config_reader.hpp"
#include "../config/walker.hpp"

#include <boilerplate/boilerplate.hpp>
#include <boilerplate/chrono.hpp>
#include <boilerplate/likely.hpp>
#include <boilerplate/piped_continuation.hpp>
#include <boilerplate/pointers.hpp>

#include <feed/feed.hpp>

#include "trigger.hpp"

#include <tuple>

template<typename trigger_map_type>
struct trigger_dispatcher
{
  trigger_map_type triggers;

  trigger_dispatcher() noexcept = default;
  explicit trigger_dispatcher(trigger_map_type &&triggers) noexcept: triggers(std::move(triggers)) {}

  bool operator()(auto &continuation, const auto &timestamp, const feed::update &update, auto &&...args) noexcept
  {
    return feed::visit_update(
      [&](auto field, const auto &value) { return (*this)(continuation, timestamp, field, value, std::forward<decltype(args)>(args)...); }, update);
  }

  template<typename continuation_type, typename field_constant_type, typename value_type, typename timestamp_type, typename... args_types>
  bool operator()(continuation_type &continuation, const timestamp_type &timestamp, field_constant_type field, const value_type &value,
                  args_types &&...args) noexcept requires std::is_same_v<typename field_constant_type::value_type, feed::field>
  {
    const auto apply = [&](auto &trigger_map_value)
    {
      auto &[fields, trigger] = trigger_map_value;
      if constexpr(boilerplate::tuple_contains_type_v<decltype(field), decltype(fields)>)
        return trigger(continuation, timestamp, value, args..., std::true_type());
      else
        return false;
    };
    return std::apply([&](auto &...triggers) { return LIKELY((apply(triggers) || ...)) || continuation(timestamp, args..., std::false_type()); },
                      triggers);
  }

  void reset(feed::instrument_state &&state) noexcept
  {
    const auto apply = [&](auto field, auto &trigger_map_value, const auto &value)
    {
      auto &[fields, trigger] = trigger_map_value;
      if constexpr(boilerplate::tuple_contains_type_v<decltype(field), decltype(fields)>)
        trigger.reset(value);
    };
    feed::visit_state([&](auto field, const auto &value) { std::apply([&](auto &...triggers) { (apply(field, triggers, value), ...); }, triggers); },
                      std::move(state));
  }

  void warm_up() noexcept
  {
    std::apply([&](auto &...trigger_map_values) { (std::get<1>(trigger_map_values).warm_up(), ...); }, triggers);
  }
};

struct polymorphic_trigger_dispatcher
{
  using instrument_closure = void *;

  using clock_type = std::chrono::steady_clock;
  using continuation_type = func::function<bool(const clock_type::time_point &, instrument_closure, bool)>;

  std::aligned_storage_t<16> storage = {};

  /*const*/ func::function<bool(void *, const continuation_type &, const clock_type::time_point &, const feed::update &, instrument_closure)> call_thunk
    = []([[maybe_unused]] auto...) { return false; };
  /*const*/ func::function<void(void *, feed::instrument_state &&)> reset_thunk = []([[maybe_unused]] auto...) {};
  /*const*/ func::function<void(void *)> warm_up_thunk = []([[maybe_unused]] auto...) {};

  template<typename upstream_dispatcher_type, typename... args_types>
  static polymorphic_trigger_dispatcher
  make(args_types &&...args) noexcept /*requires(sizeof(upstream_dispatcher_type) <= sizeof(std::declval<polymorphic_trigger_dispatcher>().storage)) */
  {
    auto result = polymorphic_trigger_dispatcher {
      .call_thunk = [](void *thiz, const continuation_type &continuation, const clock_type::time_point &timestamp, const feed::update &update,
                       instrument_closure instrument) -> bool
      {
        auto blank_dispatcher = [&](const auto &timestamp, auto closure, auto blank) -> bool { return continuation(timestamp, closure, blank()); };
        return (*reinterpret_cast<upstream_dispatcher_type *>(thiz))(blank_dispatcher, timestamp, update, instrument);
        },
      .reset_thunk = [](void *thiz, feed::instrument_state &&state) { reinterpret_cast<upstream_dispatcher_type *>(thiz)->reset(std::move(state)); },
      .warm_up_thunk = [](void *thiz) { reinterpret_cast<upstream_dispatcher_type *>(thiz)->warm_up(); }};
    new(&result.storage) upstream_dispatcher_type(std::forward<args_types>(args)...);
    return result;
  }

  bool operator()(const continuation_type &continuation, const clock_type::time_point &timestamp, const feed::update &update, instrument_closure instrument) noexcept
  {
    return call_thunk(&storage, continuation, timestamp, update, instrument);
  }
  void reset(feed::instrument_state &&state) noexcept { reset_thunk(&storage, std::move(state)); }
  void warm_up() noexcept { warm_up_thunk(&storage); }
};

struct invalid_trigger_config
{
  const config::walker &walker;
};

decltype(auto) with_trigger(const config::walker &config, boilerplate::observer_ptr<logger::logger> logger,
                  auto continuation) noexcept
{
  using namespace config::literals;

  using result_type = std::invoke_result_t<decltype(continuation), trigger_dispatcher<std::tuple<>>>;
  static_assert(boost::leaf::is_result_type<result_type>::value);

  // Look HN ! Monoid !
  const auto null_trigger = [](auto continuation) noexcept -> result_type { return continuation(std::tuple {}); };
  const auto add_trigger = [](auto &&triggers, auto &&field_set, auto &&trigger) noexcept
  {
    return std::tuple_cat(std::forward<decltype(triggers)>(triggers),
                          std::make_tuple(std::tuple(std::forward<decltype(field_set)>(field_set), std::forward<decltype(trigger)>(trigger))));
  };

  constexpr auto price_fields = std::tuple<feed::b0_c, feed::o0_c>();
#if !defined(LEAN_AND_MEAN)
  constexpr auto quantity_fields = std::tuple<feed::bq0_c, feed::oq0_c>();
#endif // !defined(LEAN_AND_MEAN)
  const auto default_price = feed::price_t {};

  auto decode_instant_move_trigger = [=](auto continuation, auto &&triggers) noexcept -> result_type
  {
    const auto instant_threshold = config["instant_threshold"_hs];
    return instant_threshold ? continuation(add_trigger(std::forward<decltype(triggers)>(triggers), price_fields,
                                                        instant_move_trigger<feed::price_t>(default_price, (feed::price_t)from_walker(instant_threshold))))
                             : continuation(std::forward<decltype(triggers)>(triggers));
  };

#if !defined(LEAN_AND_MEAN)
  auto decode_move_trigger = [=](auto continuation, auto &&triggers) noexcept -> result_type
  {
    const auto threshold = config["threshold"_hs];
    const auto period = config["period"_hs];
    const auto base = config["base"_hs];
    const auto tick_size = config["tick_size"_hs];
    if(threshold && period)
      /*if((int(base) == 3500) && (float(tick_size) == 0.5))
      {
        using base_t = feed::price_t::with_type<int>::integral_constant<3500>;
        using tick_size_t = feed::price_t::ratio<1, 2>;
        return continuation(add_trigger(std::move(triggers), price_fields, normalized_move_trigger<feed::price_t, base_t,
      tick_size_t>({}, threshold, period)));
      }
      else*/
      // TODO : use un-normalized values
      // if(trigger.bucket_overflow_period() < std::chrono::days(1)) die();
      return continuation(
        add_trigger(std::forward<decltype(triggers)>(triggers), price_fields, move_trigger<feed::price_t>({}, feed::price_t {threshold}, period)));
    else
      return continuation(std::forward<decltype(triggers)>(triggers));
  };

  auto decode_min_size_trigger = [=](auto continuation, auto &&triggers) noexcept -> result_type
  {
    const auto min_size = config["min_size"_hs];
    return min_size ? continuation(add_trigger(std::forward<decltype(triggers)>(triggers), quantity_fields, min_value_trigger<feed::quantity_t>(min_size)))
                    : continuation(std::forward<decltype(triggers)>(triggers));
  };
#endif // !defined(LEAN_AND_MEAN)

  auto check_has_trigger = [=](auto continuation, auto &&triggers) noexcept -> result_type
  {
    if constexpr(std::tuple_size_v<std::decay_t<decltype(triggers)>> != 0)
    {
#if !defined(LEAN_AND_MEAN)
      if(logger)
        logger->log(logger::info, triggers);
#endif // !defined(LEAN_AND_MEAN)
      return continuation(trigger_dispatcher<std::decay_t<decltype(triggers)>>(std::forward<decltype(triggers)>(triggers)));
    }
    else
      return BOOST_LEAF_NEW_ERROR(invalid_trigger_config {config});
  };

  using namespace piped_continuation;

  return null_trigger |= decode_instant_move_trigger
#if !defined(LEAN_AND_MEAN)
         |= decode_move_trigger |= decode_min_size_trigger
#endif // !defined(LEAN_AND_MEAN)
         |= check_has_trigger |= continuation;
}


inline auto make_polymorphic_trigger(const config::walker &config, boilerplate::observer_ptr<logger::logger> logger = nullptr) noexcept
{
  return with_trigger(config, logger, [&](auto &&trigger_dispatcher) -> boost::leaf::result<polymorphic_trigger_dispatcher> { return polymorphic_trigger_dispatcher::make<std::decay_t<decltype(trigger_dispatcher)>>(trigger_dispatcher); })();
}


#if defined(DOCTEST_LIBRARY_INCLUDED)
// GCOVR_EXCL_START

TEST_SUITE("trigger_dispatcher")
{
  TEST_CASE("polymorphic_trigger_dispatcher")
  {
    using namespace config::literals;
    using namespace std::string_view_literals;

    const auto config = "\n\
\"entrypoint.instant_threshold\": 2,\n\
\"entrypoint.threshold\": 3,\n\
\"entrypoint.period\": 10"sv;

  boost::leaf::try_handle_all(
      [&]() noexcept -> boost::leaf::result<void> {
        const auto props = BOOST_LEAF_TRYX(config::properties::create(config));
        auto trigger = BOOST_LEAF_TRYX(make_polymorphic_trigger(props["entrypoint"_hs]));
        auto send = [&](std::int64_t timestamp, feed::price_t price) {
          return trigger([](std::chrono::steady_clock::time_point timestamp, void *closure, bool for_real){ return for_real; }, std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(timestamp)), feed::encode_update(feed::field::b0, price), nullptr);
        };
        CHECK(!send(10, 0));
        CHECK(send(13, 20));
        return {};
      },
      [&]([[maybe_unused]] const boost::leaf::error_info &unmatched) noexcept { CHECK(false); });
  }
}

// GCOVR_EXCL_STOP
#endif // defined(DOCTEST_LIBRARY_INCLUDED)
