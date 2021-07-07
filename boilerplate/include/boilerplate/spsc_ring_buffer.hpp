#pragma once

#include <boilerplate/boilerplate.hpp>
#include <boilerplate/likely.hpp>
#include <boilerplate/std.hpp>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <emmintrin.h>
#include <functional>
#include <immintrin.h>
#include <limits>
#include <memory>
#include <new>
#include <tuple>
#include <x86intrin.h>

namespace bp = boilerplate;

static constexpr auto NB_CACHE_LINE_PREFETCH = 12;

class spsc_ring_buffer
{
public:
  using size_t = std::uint32_t;

private:
  static constexpr size_t CACHE_LINE_MASK = std::hardware_destructive_interference_size - 1;

  static_assert(std::is_unsigned_v<size_t>);

  const size_t buffer_size = 0;
  const size_t overflow_size = 0;
  const size_t buffer_size_mask = 0;
  std::unique_ptr<std::byte[]> buffer_ {};

  struct alignas(std::hardware_destructive_interference_size) index
  {
    std::atomic<size_t> atomic_value_ = 0;
    size_t value_ = 0;

    size_t last_flushed_index_ = 0;

    void flush(std::byte *buffer, const size_t buffer_size_mask) noexcept
    {
      const auto value_cache_line = value_ - (value_ & CACHE_LINE_MASK);
      auto last_flushed_cache_line = last_flushed_index_ - (last_flushed_index_ & CACHE_LINE_MASK);
      while(last_flushed_cache_line < value_cache_line)
      {
        // Need AVX2 instruction set
        //_mm_clflushopt(buffer + (last_flushed_cache_line & buffer_size_mask));
        _mm_clflush(buffer + (last_flushed_cache_line & buffer_size_mask));
        last_flushed_index_ = last_flushed_cache_line += std::hardware_destructive_interference_size;
      }
      atomic_value_.store(value_, std::memory_order_release);
    }

    size_t load()  noexcept { return atomic_value_.load(std::memory_order_acquire); }

    operator size_t &()  noexcept { return value_; }
  };

  index write_pos_ = {}, read_pos_ = {};

public:
  explicit spsc_ring_buffer(size_t sz = 1024 * 1024, size_t max_push_size = 128) noexcept:
    buffer_size(bp::next_power_of_2(std::max(sz, size_t(std::hardware_destructive_interference_size)))), overflow_size(max_push_size),
    buffer_size_mask(buffer_size - 1), buffer_(new(std::align_val_t {buffer_size}) std::byte[buffer_size + overflow_size])
  {
    std::memset(buffer_.get(), 0, buffer_size);

    // eject log memory from cache
    for(size_t i = 0; i < buffer_size; i += std::hardware_destructive_interference_size)
      _mm_clflush(buffer_.get() + i);
    // load first 128 cache lines into memory
    for(size_t i = 0; i < 128; ++i)
      __builtin_prefetch(buffer_.get() + (i * std::hardware_destructive_interference_size), 1, 1);
  }

  std::byte *producer_allocate(size_t size = 0) noexcept
  {
    assert(size < overflow_size);
    const auto read_pos = read_pos_.load();
    return (LIKELY(write_pos_ - read_pos + size <= buffer_size)
            // if write_pos_ has overflown and wrapped but not read_pos yet, test with read_pos wrapped too
            || UNLIKELY(write_pos_ + size <= read_pos + buffer_size))
             ? buffer_.get() + (write_pos_ & buffer_size_mask)
             : nullptr;
  }
  void producer_commit(size_t size) noexcept { write_pos_ += size; }
  void producer_flush() noexcept
  {
    write_pos_.flush(buffer_.get(), buffer_size_mask);
    __builtin_prefetch(buffer_.get() + ((write_pos_ + std::hardware_destructive_interference_size * NB_CACHE_LINE_PREFETCH) & buffer_size_mask), 1, 1);
  }

  std::byte *consumer_peek(size_t size = 0) noexcept
  {
    const auto write_pos = write_pos_.load();
    return (LIKELY(read_pos_ + size <= write_pos)
            // if write_pos_ has overflown and wrapped but not read_pos yet, test with read_pos wrapped too
            || UNLIKELY(read_pos_ + buffer_size + size <= write_pos))
             ? buffer_.get() + (read_pos_ & buffer_size_mask)
             : nullptr;
  }
  void consumer_commit(size_t size) noexcept { read_pos_ += size; }
  void consumer_flush() noexcept { read_pos_.flush(buffer_.get(), buffer_size_mask); }

  // wait for the queue to be empty. Similar to ``!consumer_peek(0)``, but from the consumer view
  bool producer_test_empty() noexcept
  {
    const auto read_pos = read_pos_.load();
    return !(LIKELY(read_pos <= write_pos_) || UNLIKELY(read_pos + buffer_size <= write_pos_));
  }
};
