#pragma once

#include <boost/container/flat_map.hpp>

#include <fstream>
#include <pthread.h>
#include <regex>
#include <system_error>

namespace bct = boost::container;

auto prefault_stack(std::size_t prefault_size) noexcept
{
  // pre-fault the stack
  pthread_attr_t attr;
  auto rc = ::pthread_getattr_np(::pthread_self(), &attr);
  if(rc != 0)
    return std::error_code(rc, std::generic_category());

  void *stack_address = nullptr;
  size_t stack_size = 0;
  rc = ::pthread_attr_getstack(&attr, &stack_address, &stack_size);
  if(rc != 0)
    return std::error_code(rc, std::generic_category());

  void *prefault_base = ::alloca(prefault_size);
  ::memset(prefault_base, 0, static_cast<std::size_t>(reinterpret_cast<std::byte *>(prefault_base) - reinterpret_cast<std::byte *>(stack_address)));

  return std::error_code();
}

class proc_maps
{
public:
  static auto &instance() noexcept
  {
    static proc_maps instance;
    return instance;
  }

  proc_maps() noexcept { update(); }

  void update() noexcept
  {
    std::ifstream input("/proc/self/maps");
    std::array<char, 128> line;
    //                          begin-addr  end-addr    permissions      offset      dev                     inode   path
    static std::regex const re("([0-9a-f]+)-([0-9a-f]+) [-r][-w][-x][-p] [0-9a-f]{8} [0-9a-f]{2}:[0-9a-f]{2} [0-9]+ +/.*");
    while(input.getline(line.data(), sizeof(line)))
    {
      std::cmatch results;
      const auto to_ptr = [](const auto &field) { return reinterpret_cast<const void *>(std::strtoll(field.first, nullptr, 16)); };
      if(std::regex_match(line.data(), results, re))
        segments.emplace(to_ptr(results[2]), to_ptr(results[1]));
    }
  }

  bool is_in_code_segment(const void *ptr) const noexcept
  {
    auto it = segments.upper_bound(ptr);
    return (it != segments.end()) && (ptr > it->second);
  }

  template<typename value_type>
  void check([[maybe_unused]] value_type _) const noexcept
  {
  }

  // not owning references -> make sure it's in a code segment
  void check(const char *value) const noexcept { assert(is_in_code_segment(value)); }

  void check(const std::string_view &value) const noexcept { assert(is_in_code_segment(value.data())); }

  // owning references -> make sure it's NOT in a code segment
  void check(const std::string &value) const noexcept { assert(!is_in_code_segment(value.data())); }

  template<typename value_type>
  void check([[maybe_unused]] const std::reference_wrapper<value_type> &_) const noexcept
  {
  }

private:
  bct::flat_map<const void *, const void *> segments {};
};

