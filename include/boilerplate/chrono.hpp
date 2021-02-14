#pragma once

#include <boilerplate/std.hpp>

#include <chrono>

struct data
{
  uint8_t node, cpu;
  uint64_t tsc;
};

inline data rdtscp() noexcept
{
  uint32_t eax {}, ecx {}, edx {};
  asm volatile("rdtscp" : "=a"(eax), "=d"(edx), "=c"(ecx));
  return {uint8_t((ecx & 0xFFF000U) >> 12U), uint8_t(ecx & 0xFFFU), (uint64_t(edx) << 32U) | eax}; // NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
}

struct incomplete_nano_clock
{
  using rep = std::int64_t;
  using period = std::nano;
  using duration = std::chrono::duration<rep, period>;
  using time_point = std::chrono::time_point<incomplete_nano_clock>;
  static constexpr bool is_steady = false;
};

struct nano_clock : public incomplete_nano_clock
{
  static time_point now() { return time_point(std::chrono::duration_cast<duration>(std::chrono::high_resolution_clock::now().time_since_epoch())); }
};

#if defined(USE_TCPDIRECT)
using network_clock = fantasy_nano_clock;
#else // defined(USE_TCPDIRECT)
using network_clock = nano_clock;
#endif // defined(USE_TCPDIRECT)

constexpr std::timespec to_timespec(const std::chrono::nanoseconds &duration) noexcept { return {.tv_sec = static_cast<std::time_t>(duration.count() / 1'000'000'000), .tv_nsec = static_cast<long>(duration.count() % 1'000'000'000)}; }
template<typename time_point_type>
constexpr std::timespec to_timespec(const time_point_type &time_point) noexcept requires std::chrono::is_clock_v<typename time_point_type::clock> { return to_timespec(time_point.time_since_epoch()); }

constexpr ::timeval to_timeval(const std::chrono::nanoseconds &duration) noexcept { return {.tv_sec = static_cast<std::time_t>(duration.count() / 1'000'000'000), .tv_usec = static_cast<long>(duration.count() % 1'000'000'000) / 1000}; }

template<typename clock_type>
constexpr typename clock_type::time_point to_time_point(const std::timespec &timespec) noexcept requires std::chrono::is_clock_v<clock_type> { return typename clock_type::time_point(std::chrono::nanoseconds(timespec.tv_sec * clock_type::period::den + timespec.tv_nsec)); }
