#include <boilerplate/chrono.hpp>

#include <cstdio>
#include <cstdlib>
#include <thread>

static_assert(std::chrono::high_resolution_clock::is_steady);

int main(int argc, char *argv[])
{
  if(argc < 2)
    std::abort();

  const auto calibration_period = std::chrono::seconds(::atoi(argv[1]));

  const auto start_time = std::chrono::high_resolution_clock::now();
  asm volatile("" : : "g"(start_time) : "memory");
  const auto start_data = rdtscp();
  asm volatile("cpuid" ::: "%rax", "%rbx", "%rcx", "%rdx");

  std::this_thread::sleep_for(calibration_period);

  const auto end_data = rdtscp();
  asm volatile("cpuid" ::: "%rax", "%rbx", "%rcx", "%rdx");
  const auto end_time = std::chrono::high_resolution_clock::now();

  if(std::tuple(start_data.node, start_data.cpu) != std::tuple(end_data.node, end_data.cpu))
    // core migration during calibration
    std::abort();

  const auto hz = (long double)(end_data.tsc - start_data.tsc) / (long double)(std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count());
  std::printf("%.33Lf", hz);

  return 0;
}