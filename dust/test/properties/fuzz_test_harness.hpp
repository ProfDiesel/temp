#pragma once

#include "feed/feed.hpp"

#include <asio/buffer.hpp>

//#include <boost/noncopyable.hpp>
#include <boost/leaf/result.hpp>

#include <limits>
#include <random>


namespace fuzz
{

void bad_test() { ::exit(0); }
void failed_test() { ::abort(); }

/*
class generator : public boost::noncopyable
{
public:
  using result_type = unsigned int;

  explicit generator(std::string_view path = "/dev/urandom") noexcept: fd(::open(path.data(), 0)), is_owner(true) {}
  explicit generator(int fd, bool is_owner = false) noexcept: fd(fd), is_owner(is_owner) {}
  ~generator() noexcept { ::close(fd); }

  static constexpr result_type min() noexcept { return std::numeric_limits<result_type>::min(); }
  static constexpr result_type max() noexcept { return std::numeric_limits<result_type>::max(); }

  auto operator()() noexcept
  {
    result_type result;
    if(::read(fd, &result, sizeof(result)) != sizeof(result))
      bad_test();
    return result;
  }

  auto fill(const asio::mutable_buffer &buffer) noexcept
  {
    const auto nb_read = ::read(fd, buffer.data(), buffer.size());
    if(nb_read < 0)
      bad_test();
    return asio::buffer(buffer.data(), nb_read);
  }

private:
  const int fd = STDIN_FILENO;
  const bool is_owner = false;
};
*/

class generator : public boost::noncopyable
{
public:
  using result_type = unsigned int;

  explicit generator(const asio::const_buffer &buffer) noexcept: buffer(buffer) {}

  static constexpr result_type min() noexcept { return std::numeric_limits<result_type>::min(); }
  static constexpr result_type max() noexcept { return std::numeric_limits<result_type>::max(); }

  result_type operator()() noexcept
  {
    result_type result;
    if(fill(asio::buffer(&result, sizeof(result))).size() != sizeof(result))
      bad_test();
    return result;
  }

  asio::mutable_buffer fill(const asio::mutable_buffer &buffer) noexcept
  {
    const auto size = asio::buffer_copy(buffer, this->buffer);
    if(size < 0)
      bad_test();
    this->buffer += size;
    return asio::buffer(buffer.data(), size);
  }

  template<typename value_type>
  auto get() noexcept
  {
    value_type value;
    fill(asio::buffer(&value, sizeof(value)));
    return value;
  }

private:
  asio::const_buffer buffer;
};

struct feeder
{
  fuzz::generator gen;
  network_clock::time_point current_timestamp;
  // std::uniform_int_distribution<network_clock::rep> clock_distribution {0, 1'000'000'000};

      // current_timestamp += network_clock::duration(clock_distribution(gen));


/*
    auto random_duration = [&]() {
      std::uniform_int_distribution<clock::rep> distribution(0, 1'000'000'000);
      return clock::duration {distribution(gen)};
    };

    auto random_size = [&](std::size_t max) {
      std::uniform_int_distribution<std::size_t> distribution(0, max);
      return distribution(gen);
    };

    auto random_price = [&]() {
      constexpr auto min_price = 90.0;
      constexpr auto max_price = 200.0;
      std::uniform_real_distribution<float> distribution(min_price, max_price);
      return feed::price_t {std::decimal::decimal32 {distribution(gen)}};
    };

    auto random_quantity = [&]() {
      std::uniform_int_distribution<std::size_t> distribution;
      return distribution(gen);
    };
*/


};
}
