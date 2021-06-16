#pragma once

#include "feed/feed.hpp"

#include <boilerplate/chrono.hpp>

#include <chrono>
#include <functional>

namespace backtest
{

struct event final
{
  network_clock::rep timestamp;
  feed::detail::packet packet;

  auto time_point() const noexcept { return network_clock::time_point(network_clock::duration(timestamp)); }
} __attribute__((packed));

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

class buffer_feeder
{
public:
  buffer_feeder(const asio::mutable_buffer &buffer) noexcept: buffer_(buffer) {}

  boost::leaf::awaitable<boost::leaf::result<feed::instrument_state>> on_snapshot_request(feed::instrument_id_type instrument) noexcept
  {
    co_return BOOST_LEAF_NEW_ERROR(std::make_error_code(std::errc::io_error)); // TODO
  }

  boost::leaf::result<void> on_update_poll(auto &&continuation) noexcept
  {
    if(buffer_.size() < sizeof(event))
      return BOOST_LEAF_NEW_ERROR(std::make_error_code(std::errc::io_error));

    auto *current = reinterpret_cast<const event*>(buffer_.data());
    current_timestamp_ = std::max(current_timestamp_, current->time_point());
    buffer_ += offsetof(event, packet);

    for(;;)
    {
      const auto sanitized = feed::detail::sanitize(boilerplate::overloaded {
                                                      [&](auto field, feed::price_t value) { return value; },
                                                      [&](auto field, feed::quantity_t value) { return value; },
                                                    },
                                                    buffer_);
      std::forward<decltype(continuation)>(continuation)(current_timestamp_, buffer_);
      buffer_ += sanitized;
    }

    return boost::leaf::success();
  }

  auto make_snapshot_requester() noexcept
  {
    return [this](feed::instrument_id_type instrument) noexcept { return on_snapshot_request(instrument); };
  }

  auto make_update_source() noexcept
  {
    return [this](auto &&continuation) noexcept { return on_update_poll(std::forward<decltype(continuation)>(continuation)); };
  }

private:
  network_clock::time_point current_timestamp_;
  asio::mutable_buffer buffer_;
};

} // namespace backtest
