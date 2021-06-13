#pragma once

#include <chrono>
#include <boilerplate/chrono.hpp>
#include "feed_binary.hpp"

namespace backtest
{

struct event final
{
  nano_clock::time_point timestamp;
  feed::detail::packet packet;
} __attribute__((packed));

class scenario
{
};

class deterministic_executor
{
public:
  using action_type = func::function<void(void)>;

  void add(const std::chrono::steady_clock::duration &delay, const action_type &action) { actions.emplace(nano_clock::now() + delay, action); }

  void poll()
  {
    if(actions.empty())
      return;

    const auto first = actions.begin(), last = actions.upper_bound(nano_clock::now() = first->first);
    std::for_each(first, last, [&](const auto &value) { value.second(); });
    actions.erase(first, last);
  }

private:
  static constexpr std::chrono::steady_clock::duration granularity = std::chrono::microseconds(1);
  boost::container::flat_multimap<nano_clock::time_point, action_type> actions;
};

struct feeder
{
  fuzz::generator gen;
  network_clock::time_point current_timestamp;

  static constexpr auto max_nb_message_per_packet = 3;
  static constexpr auto input_buffer_size = feed::detail::packet_header_size + max_nb_message_per_packet * feed::detail::message_max_size;

  boost::leaf::awaitable<boost::leaf::result<feed::instrument_state>> on_snapshot_request(feed::instrument_id_type instrument) noexcept
  {
    co_return BOOST_LEAF_NEW_ERROR(std::make_error_code(std::errc::io_error)); // TODO
  }

  boost::leaf::result<void> on_update_poll(func::function<void(network_clock::time_point, asio::const_buffer &&)> continuation) noexcept
  {
    std::aligned_storage<input_buffer_size, alignof(feed::message)> buffer_storage;
    std::byte *const first = reinterpret_cast<std::byte *>(&buffer_storage), *last = first, *const end = first + input_buffer_size;

    for(;;)
    {
      const auto filled = gen.fill(asio::buffer(last, end - last));
      last += filled.size();

      current_timestamp += std::chrono::nanoseconds(gen.get<std::uint32_t>());

      auto buffer = asio::buffer(first, last - first);
      const auto sanitized = feed::detail::sanitize(boilerplate::overloaded {
                                                      [&](auto field, feed::price_t value) { return value; },
                                                      [&](auto field, feed::quantity_t value) { return value; },
                                                    },
                                                    buffer);
      continuation(current_timestamp, buffer);

      last = std::copy(first + sanitized, last, first);
    }
  }

  auto make_snapshot_requester() noexcept
  {
    return [this](feed::instrument_id_type instrument) noexcept { return on_snapshot_request(instrument); };
  }

  auto make_update_source() noexcept
  {
    return [this](func::function<void(network_clock::time_point, const asio::const_buffer &)> continuation) noexcept { return on_update_poll(continuation); };
  }
};


} // namespace backtest
