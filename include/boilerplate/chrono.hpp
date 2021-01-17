#pragma once

#include <chrono>
#include <fstream>
#include <regex>
#include <string>
#include <thread>

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

inline void cpuid() noexcept { asm volatile("cpuid" ::: "%rax", "%rbx", "%rcx", "%rdx"); }

template<typename value_type>
value_type &&retain(value_type &&value) noexcept
{
#if !defined(CRIPPLED)
  asm volatile("" : : "g"(value) : "memory");
#endif // !defined(CRIPPLED)
  return std::forward<value_type &&>(value);
}

inline double tsc_per_ns() noexcept
{
  using namespace std::chrono_literals;

  // static_assert(std::chrono::high_resolution_clock::is_steady);

  const auto start_time = retain(std::chrono::high_resolution_clock::now());
  auto start_data = rdtscp();
  cpuid();

  std::this_thread::sleep_for(10s);

  auto end_data = rdtscp();
  cpuid();
  const auto end_time = std::chrono::high_resolution_clock::now();

  if(std::tuple(start_data.node, start_data.cpu) != std::tuple(end_data.node, end_data.cpu))
    // core migration during calibration
    return 0;

  return double(end_data.tsc - start_data.tsc) / double(std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count());
}

inline double cpu_freq() noexcept
{
  std::ifstream input("/proc/cpuinfo");
  std::string line;
  static const std::regex mhz("cpu MHz\\s*: (.*)");
  while(std::getline(input, line))
  {
    std::smatch results;
    if(std::regex_match(line, results, mhz))
    {
      return std::stod(results[1]);
    }
  }

  return {};
}

struct clock
{
  using rep = std::int64_t;
  using period = std::nano;
  using duration = std::chrono::duration<rep, period>;
  using time_point = std::chrono::time_point<clock>;
  static const bool is_steady = false;

  static time_point epoch() noexcept { return {}; }
#if !defined(USE_TCPDIRECT)
  static time_point now() noexcept { return time_point(std::chrono::duration_cast<duration>(std::chrono::high_resolution_clock::now().time_since_epoch())); }
#endif // !defined(USE_TCPDIRECT)
};

constexpr std::timespec to_timespec(clock::duration duration) noexcept { return {.tv_sec = static_cast<std::time_t>(duration.count() / clock::period::den), .tv_nsec = static_cast<long>(duration.count() % clock::period::den)}; }
constexpr std::timespec to_timespec(clock::time_point time_point) noexcept { return to_timespec(time_point.time_since_epoch()); }
constexpr ::timeval to_timeval(clock::duration duration) noexcept { return {.tv_sec = static_cast<std::time_t>(duration.count() / clock::period::den), .tv_usec = static_cast<long>(duration.count() % clock::period::den) / 1000}; }
constexpr clock::time_point to_time_point(std::timespec timespec) noexcept { return clock::time_point(clock::duration(timespec.tv_sec * clock::period::den + timespec.tv_nsec)); }

