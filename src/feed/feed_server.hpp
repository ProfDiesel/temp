#include "feed.hpp"

#include <boilerplate/leaf.hpp>
#include <boilerplate/logger.hpp>
#include <boilerplate/outcome.hpp>
#include <boilerplate/pointers.hpp>

#include <asio/awaitable.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/buffer.hpp>

#include <unordered_map>

#define OUTCOME_CO_TRY_OP_LENGTH(op, stream, data, length)                                                                                                         \
  if(OUTCOME_CO_TRYX(co_await op(socket, asio::buffer(data, length), as_result(asio::use_awaitable))) != length)                                               \
    co_return out::failure(std::make_error_code(std::errc::io_error)); // TODO

#define OUTCOME_CO_TRY_READ_LENGTH(stream, data, length) OUTCOME_CO_TRY_OP_LENGTH(asio::async_read, stream, data, length)
#define OUTCOME_CO_TRY_READ(stream, data) OUTCOME_CO_TRY_READ_LENGTH(stream, &data, sizeof(data))

#define OUTCOME_CO_TRY_WRITE_LENGTH(stream, data, length) OUTCOME_CO_TRY_OP_LENGTH(asio::async_write, stream, data, length)
#define OUTCOME_CO_TRY_WRITE(stream, data) OUTCOME_CO_TRY_WRITE_LENGTH(stream, &data, sizeof(data))

namespace feed {

struct server;

struct session : public std::enable_shared_from_this<session>
{
  asio::ip::tcp::socket socket;
  boilerplate::not_null_observer_ptr<server> server;

  session(asio::ip::tcp::socket &&socket, boilerplate::not_null_observer_ptr<struct server> server) noexcept: socket(std::move(socket)), server(server) {}

  asio::awaitable<out::result<void>> operator()() noexcept;
};

struct server
{
  asio::io_context &service;
  asio::ip::udp::socket updates_socket;
  asio::ip::udp::endpoint updates_endpoint;
  asio::ip::tcp::acceptor snapshot_acceptor;

  struct state
  {
    instrument_state state;
    decltype(instrument_state::updates) accumulated_updates;
  };

  std::unordered_map<instrument_id_type, state> states {};

  server(asio::io_context &service, const asio::ip::tcp::endpoint &snapshot_endpoint, const asio::ip::udp::endpoint &updates_endpoint):
    service(service), updates_socket(service), updates_endpoint(updates_endpoint), snapshot_acceptor(service, snapshot_endpoint)
  {
  }

  void reset(instrument_id_type instrument, instrument_state state = {}) { states[instrument] = {state, state.updates}; }

  asio::awaitable<void> update(const std::vector<std::tuple<instrument_id_type, instrument_state>> &states)
  {
    std::aligned_storage_t<detail::packet_max_size, alignof(detail::packet)> storage;
    auto packet = new (&storage) detail::packet {static_cast<std::uint8_t>(states.size()), {}};
    asio::mutable_buffer buffer(&packet->message, sizeof(storage) - offsetof(detail::packet, message));

    auto current = buffer;
    for(auto &&[instrument, new_state]: states)
    {
      auto &[state, accumulated_updates] = this->states[instrument];
      visit_state([&state=state](auto field, auto value) { update_state(state, field, value); }, new_state);
      state.sequence_id = new_state.sequence_id;
      current += detail::encode_message(instrument, state, current);
      accumulated_updates |= std::exchange(state.updates, {});
    }

    co_await updates_socket.async_send_to(buffer, updates_endpoint, asio::use_awaitable);
  }

  instrument_state snapshot(instrument_id_type instrument) const noexcept
  {
    const auto it = states.find(instrument);
    if(it == states.end())
      return {};
    auto result = it->second.state;
    result.updates = it->second.accumulated_updates;
    return result;
  }

  asio::awaitable<out::result<void>> accept() noexcept
  {
    asio::ip::tcp::socket socket(service);
    OUTCOME_CO_TRY(co_await snapshot_acceptor.async_accept(socket, as_result(asio::use_awaitable)));
    auto session_ptr = std::make_shared<session>(std::move(socket), boilerplate::make_strict_not_null(this));
    asio::co_spawn(service, [&]() noexcept -> asio::awaitable<void> { 
      auto result = co_await (*session_ptr)();
      if(!result)
      {
        using namespace logger::literals;
        // logger->log(logger::critical, "{}."_format, result.assume_error());
      }
    }, asio::detached);

    co_return out::success();
  }
};

inline asio::awaitable<out::result<void>> session::operator()() noexcept
{
  const auto self(shared_from_this());

  detail::snapshot_request request;
  OUTCOME_CO_TRY_READ(socket, request);

  std::array<char, 128> buffer;
  const auto actual_length
    = feed::detail::encode_message(request.instrument.value(), server->snapshot(request.instrument.value()), asio::mutable_buffer(buffer.data(), buffer.size()));
  OUTCOME_CO_TRY_WRITE_LENGTH(socket, buffer.data(), actual_length);
  co_return out::success();
}

} // namespace feed
