#pragma once

#include <boilerplate/boilerplate.hpp>
#include <boilerplate/fmt.hpp>
#include <boilerplate/likely.hpp>
#include <boilerplate/std.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string_view>

template<typename value_type>
class min_value_trigger
{
public:
  explicit min_value_trigger(const value_type &threshold) noexcept: threshold(threshold) {}

  void reset([[maybe_unused]] const value_type &_) noexcept {}

  void warm_up() noexcept { ::__builtin_prefetch(&threshold, 1, 1); }

  template<typename continuation_type, typename timestamp_type, typename... args_types>
  [[using gnu : always_inline, flatten, hot]] inline auto operator()(continuation_type &continuation, const timestamp_type &timestamp, const value_type &value, args_types &&...args) noexcept
  {
    if(value < threshold)
      return continuation(timestamp, std::forward<args_types>(args)...);
    return decltype(continuation(timestamp, std::forward<args_types>(args)...)) {};
  }

private:
  const value_type threshold {};
};

template<typename value_type>
struct fmt::formatter<min_value_trigger<value_type>, char> : default_formatter<min_value_trigger<value_type>, char>
{
};

template<typename value_type>
class instant_move_trigger
{
public:
  instant_move_trigger(const value_type &initial_value, const value_type &threshold) noexcept:
    bounds({initial_value - threshold, initial_value + threshold}), threshold(threshold)
  {
    reset(initial_value);
  }

  void reset(const value_type &initial_value) noexcept { bounds = {initial_value - threshold, initial_value + threshold}; }

  void warm_up() noexcept { ::__builtin_prefetch(&bounds, 1, 1); }

  template<typename continuation_type, typename timestamp_type, typename... args_types>
  [[using gnu : always_inline, flatten, hot]] inline auto operator()(continuation_type &continuation, const timestamp_type &timestamp, const value_type &value, args_types &&...args) noexcept
  {
    const auto &[lower, upper] = bounds;
    decltype(continuation(timestamp, std::forward<args_types>(args)...)) result {};

    if((value < lower) || (value > upper))
      result = continuation(timestamp, std::forward<args_types>(args)...);
    bounds = std::tuple(value - threshold, value + threshold);
    return result;
  }

private:
  std::tuple<value_type, value_type> bounds {};
  const value_type threshold {};
};

template<typename value_type>
struct fmt::formatter<instant_move_trigger<value_type>, char> : default_formatter<instant_move_trigger<value_type>, char>
{
};

template<typename value_type, typename period_type = std::chrono::nanoseconds, std::size_t nb_buckets = 8>
class move_trigger
{
public:
  move_trigger(const value_type &initial_value, const value_type &threshold, const period_type &period) noexcept:
    threshold(threshold), timestamp_to_bucket_rshift(boilerplate::required_bits(period.count() / nb_buckets))
  {
    reset(initial_value);
  }

  constexpr auto actual_period() const noexcept { return nb_buckets * period_type(1U << timestamp_to_bucket_rshift); }
  constexpr auto bucket_overflow_period() const noexcept
  {
    return period_type(static_cast<std::make_unsigned_t<typename period_type::rep>>(period_type::max().count()) >> timestamp_to_bucket_rshift);
  }

  void reset(const value_type &initial_value) noexcept { bounds.fill(std::tuple(initial_value - threshold, initial_value + threshold)); }

  void warm_up() noexcept { ::__builtin_prefetch(&bounds, 1, 1); }

  template<typename continuation_type, typename timestamp_type, typename... args_types>
  [[using gnu : always_inline, flatten, hot]] inline auto operator()(continuation_type &continuation, const timestamp_type &timestamp, const value_type &value, args_types &&...args) noexcept
  {
    decltype(continuation(timestamp, std::forward<args_types>(args)...)) result {};
    decltype(last_bucket) current_bucket = static_cast<decltype(last_bucket)>(timestamp.time_since_epoch().count() >> timestamp_to_bucket_rshift);

    assert(((value - threshold) == static_cast<value_type>(value - threshold)) && ((value + threshold) == static_cast<value_type>(value + threshold)));

    if(LIKELY((current_bucket - last_bucket) <= nb_buckets))
    {
      for(; last_bucket < current_bucket; ++last_bucket)
      {
        const auto &[lower, upper] = bounds[last_bucket & (nb_buckets - 1)];
        if((value < lower) || (value > upper))
        {
          result = continuation(timestamp, std::forward<args_types>(args)...);
          break;
        }
      }
      auto &bucket = bounds[last_bucket & (nb_buckets - 1)];
      const auto &[last_lower, last_upper] = bucket;
      const auto [lower, upper] = std::tuple(static_cast<value_type>(value - threshold), static_cast<value_type>(value + threshold));
      bucket = current_bucket == last_bucket ? std::tuple(std::max(last_lower, lower), std::min(last_upper, upper))
                                             : std::tuple(lower, upper);
    }
    else
    {
      reset(value);
      last_bucket = current_bucket;
    }

    return result;
  }

private:
  static_assert(boilerplate::next_power_of_2(nb_buckets) == nb_buckets);

  using min_max_type = std::tuple<value_type, value_type>;

  const value_type threshold {};
  const unsigned int timestamp_to_bucket_rshift {};

  unsigned int last_bucket {};
  std::array<min_max_type, nb_buckets> bounds {};
};

template<typename value_type>
struct fmt::formatter<move_trigger<value_type>, char> : default_formatter<move_trigger<value_type>, char>
{
};

template<typename value_type, typename base_integral_type, typename tick_size_ratio_type, typename normalized_value_type = std::uint16_t, typename period_type=std::chrono::nanoseconds>
class normalized_move_trigger
{
public:
  static constexpr auto nb_buckets = std::hardware_destructive_interference_size / sizeof(value_type) / 2;
  static constexpr auto inv_tick = static_cast<value_type>(tick_size_ratio_type::den) / tick_size_ratio_type::num;

  move_trigger<normalized_value_type, period_type, nb_buckets> upstream;

  normalized_move_trigger(const value_type &initial_value, const value_type &threshold, const period_type &period) noexcept:
    upstream(normalize(initial_value), normalize_difference(threshold), period)
  {
  }

  constexpr auto actual_period() const noexcept { return upstream.actual_period(); }

  void reset(const value_type &initial_value) noexcept { upstream.reset(normalize(initial_value)); }

  void warm_up() noexcept { upstream.warm_up(); }

  template<typename continuation_type, typename timestamp_type, typename... args_types>
  [[using gnu : always_inline, flatten, hot]] inline auto operator()(continuation_type &continuation, const timestamp_type &timestamp, const value_type &value, args_types &&...args) noexcept
  {
    return upstream(continuation, timestamp, normalize(value), std::forward<args_types>(args)...);
  }

  static constexpr normalized_value_type normalize_difference(const value_type &value) noexcept { return value * inv_tick; }
  static constexpr normalized_value_type normalize(const value_type &value) noexcept { return normalize_difference(value - base_integral_type::value); }

  static_assert(sizeof(upstream) <= std::hardware_destructive_interference_size);
};

namespace reference_implementation
{
template<typename value_type, typename period_type=std::chrono::nanoseconds>
class move_trigger
{
public:
  move_trigger(const value_type &initial_value, const value_type &threshold, const period_type &period) noexcept: threshold(threshold), period(period)
  {
    reset(initial_value);
  }

  constexpr auto actual_period() const noexcept { return period; }

  void reset(const value_type &initial_value) noexcept
  {
    this->initial_value = initial_value;
    histo.clear();
  }

  template<typename continuation_type, typename timestamp_type, typename... args_types>
  [[using gnu : always_inline, flatten, hot]] inline auto operator()(continuation_type &continuation, const timestamp_type &timestamp, const value_type &value, args_types &&...args) noexcept
  {
    decltype(continuation(timestamp, std::forward<args_types>(args)...)) result {};

    const auto check = [&](const value_type &histo_value) { return (histo_value >= value - threshold) && (histo_value <= value + threshold); };

    const auto it = std::find_if(histo.begin(), histo.end(), [&](const auto &entry) { return std::get<0>(entry) <= timestamp.time_since_epoch() - period; });
    if(it != histo.end())
      histo.erase(it, histo.end());

    if(initial_value)
    {
      if(!check(*std::exchange(initial_value, std::nullopt)))
        result = continuation(timestamp, std::forward<args_types>(args)...);
    }
    else
    {
      for(auto &&[_, histo_value]: histo)
      {
        if(!check(histo_value))
        {
          result = continuation(timestamp, std::forward<args_types>(args)...);
          break;
        }
      }
    }

    histo.push_front(std::tuple {timestamp.time_since_epoch(), value});

    return result;
  }

private:
  std::optional<value_type> initial_value {};
  std::deque<std::tuple<period_type, value_type>> histo {};
  const value_type threshold {};
  const period_type period {};
};
} // namespace reference_implementation

#if defined(DOCTEST_LIBRARY_INCLUDED)
// GCOVR_EXCL_START
TYPE_TO_STRING(move_trigger<int>);
TYPE_TO_STRING(reference_implementation::move_trigger<int, std::chrono::high_resolution_clock::duration>);

TEST_SUITE("trigger")
{
  const auto continuation = []([[maybe_unused]] auto timestamp) {
    return true; };
  using namespace std::literals::chrono_literals;

  TEST_CASE("min_value_trigger")
  {
    min_value_trigger<int> trigger(10);

    std::chrono::high_resolution_clock::time_point timestamp;
    CHECK(trigger(continuation, timestamp, 9));
    CHECK(!trigger(continuation, timestamp, 10));
    CHECK(!trigger(continuation, timestamp, 11));
  }

  TEST_CASE("instant_move_trigger")
  {
    instant_move_trigger<int> trigger(10, 2);

    std::chrono::high_resolution_clock::time_point timestamp;
    CHECK(!trigger(continuation, timestamp, 9));
    CHECK(!trigger(continuation, timestamp, 8));

    trigger.reset(10);
    CHECK(trigger(continuation, timestamp, 7));
    CHECK(trigger(continuation, timestamp, 10));
    CHECK(!trigger(continuation, timestamp, 11));
    CHECK(trigger(continuation, timestamp, 14));
  }

  TEST_CASE_TEMPLATE("move_trigger", T, move_trigger<float, std::chrono::high_resolution_clock::duration>, normalized_move_trigger<float, std::integral_constant<int, 8>, std::ratio<5, 10>>, reference_implementation::move_trigger<float, std::chrono::high_resolution_clock::duration>)
  {
    T trigger(10.0, 1.0, 10ms);

    SUBCASE("inside_period")
    {
      std::chrono::high_resolution_clock::time_point timestamp;
      CHECK(!trigger(continuation, timestamp, 9.5));
      timestamp += 1ms;
      CHECK(!trigger(continuation, timestamp, 10.0));
      timestamp += 1ms;
      CHECK(!trigger(continuation, timestamp, 10.5));
      timestamp += 1ms;
      CHECK(trigger(continuation, timestamp, 11.0));
      timestamp += 1ms;
      CHECK(trigger(continuation, timestamp, 9.0));
      timestamp += trigger.actual_period();
      CHECK(!trigger(continuation, timestamp, 11.0));
      timestamp += trigger.actual_period() + 1ms;
      CHECK(!trigger(continuation, timestamp, 9.0));
    }
  }

  TEST_CASE("move_trigger ext")
  {
    move_trigger<int, std::chrono::high_resolution_clock::duration> trigger(10, 2, 10ms);

    // known limitation (but defined behavior): no trigger at the edge of the bucket_overflow_period
    SUBCASE("bucket_overflow_period")
    {
      std::chrono::high_resolution_clock::time_point timestamp(trigger.bucket_overflow_period() - 1ms);
      CHECK(!trigger(continuation, timestamp, 10));
      timestamp += 1ms;
      CHECK(!trigger(continuation, timestamp, 7));
      timestamp += 1ms;
      CHECK(trigger(continuation, timestamp, 10));
    }
  }
}

// GCOVR_EXCL_STOP
#endif // defined(DOCTEST_LIBRARY_INCLUDED)
