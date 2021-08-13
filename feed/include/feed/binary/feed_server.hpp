#pragma once

#include <feed/feed.hpp>

#include <boilerplate/leaf.hpp>
#include <boilerplate/logger.hpp>
#include <boilerplate/pointers.hpp>

#include <asio/awaitable.hpp>
#include <asio/buffer.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/ip/udp.hpp>

#include <boost/container/flat_map.hpp>

#include <unordered_map>

namespace feed
{
struct server;

namespace detail
{
struct session : public std::enable_shared_from_this<session>
{
  asio::ip::tcp::socket socket;
  boilerplate::not_null_observer_ptr<server> server_ptr;

  session(asio::ip::tcp::socket &&socket, boilerplate::not_null_observer_ptr<server> server_ptr) noexcept: socket(std::move(socket)), server_ptr(server_ptr) {}

  boost::leaf::awaitable<boost::leaf::result<void>> operator()() noexcept;
};
} // namespace detail

class server : public state_map
{
public:
  server(asio::io_context &service) noexcept: service(service), updates_socket(service), snapshot_acceptor(service) {}

  boost::leaf::result<void> connect(const asio::ip::tcp::endpoint &snapshot_endpoint, const asio::ip::udp::endpoint &updates_endpoint) noexcept
  {
    BOOST_LEAF_EC_TRYV(updates_socket.open(updates_endpoint.protocol(), _));
    BOOST_LEAF_EC_TRYV(updates_socket.connect(updates_endpoint, _));

    BOOST_LEAF_EC_TRYV(snapshot_acceptor.open(snapshot_endpoint.protocol(), _));
    BOOST_LEAF_EC_TRYV(snapshot_acceptor.set_option(asio::ip::tcp::socket::reuse_address(true), _));
    BOOST_LEAF_EC_TRYV(snapshot_acceptor.bind(snapshot_endpoint, _));
    BOOST_LEAF_EC_TRYV(snapshot_acceptor.listen(asio::ip::tcp::socket::max_listen_connections, _));

    return boost::leaf::success();
  }

  boost::leaf::awaitable<boost::leaf::result<void>>
  update(const auto &states) noexcept // TODO requires is_iterable<decltype(states), std::tuple<instrument_id_type, instrument_state>>
  {
    BOOST_LEAF_ASIO_CO_TRYV(co_await state_map::update(states, [&](auto &&buffer) { return updates_socket.async_send(buffer, _); }));
    co_return boost::leaf::success();
  }

  instrument_state snapshot(instrument_id_type instrument) const noexcept { return at(instrument); }

  boost::leaf::awaitable<boost::leaf::result<void>> accept() noexcept
  {
    asio::ip::tcp::socket socket(service);
    BOOST_LEAF_ASIO_CO_TRYV(co_await snapshot_acceptor.async_accept(socket, _));
    auto session_ptr = std::make_shared<detail::session>(std::move(socket), boilerplate::make_strict_not_null(this));
    asio::co_spawn(
      service,
      [session_ptr]() mutable noexcept -> boost::leaf::awaitable<void>
      {
        co_await boost::leaf::co_try_handle_all(
          *session_ptr,
          [&](const boost::leaf::error_info &unmatched) {
            // TODO
            // logger->log(logger::critical, "leaf_error_id={} exited"_format, ei.error());
          });
        co_return;
      },
      asio::detached);

    co_return boost::leaf::success();
  }

private:
  asio::io_context &service;
  asio::ip::udp::socket updates_socket;
  asio::ip::tcp::acceptor snapshot_acceptor;
};

inline boost::leaf::awaitable<boost::leaf::result<void>> detail::session::operator()() noexcept
{
  const auto self(shared_from_this());

  detail::snapshot_request request;
  BOOST_LEAF_ASIO_CO_TRYV(co_await asio::async_read(socket, asio::buffer(&request, sizeof(request)), _));

  std::array<char, 128> buffer;
  const auto actual_length = feed::detail::encode_message(request.instrument.value(), server_ptr->snapshot(request.instrument.value()),
                                                          asio::mutable_buffer(buffer.data(), buffer.size()));
  BOOST_LEAF_ASIO_CO_TRYV(co_await asio::async_write(socket, asio::buffer(buffer.data(), actual_length), _));

  co_return boost::leaf::success();
}

boost::leaf::awaitable<boost::leaf::result<void>> replay(auto co_continuation, auto co_wait_until, asio::const_buffer buffer)
{
  const auto *current = reinterpret_cast<const feed::detail::event *>(buffer.data());
  const auto timestamp_0 = network_clock::time_point(std::chrono::nanoseconds(current->timestamp));

  boost::container::flat_map<instrument_id_type, instrument_state> states;
  while(buffer.size() > sizeof(feed::detail::event))
  {
    const auto *current = reinterpret_cast<const feed::detail::event *>(buffer.data());
    const auto timestamp = network_clock::time_point(std::chrono::nanoseconds(current->timestamp));
    BOOST_LEAF_CO_TRYV(co_await co_wait_until(timestamp - timestamp_0));
    buffer += offsetof(feed::detail::event, packet);

    const auto decoded = feed::detail::decode([](auto instrument_id, [[maybe_unused]] auto sequence_id) { return instrument_id; },
                                              [&states]([[maybe_unused]] const auto &timestamp, const auto &update, const auto &instrument_closure)
                                              { update_state(states[instrument_closure], update); },
                                              timestamp, buffer);

    BOOST_LEAF_CO_TRYV(co_await co_continuation(std::move(states)));
    states.clear();

    buffer += decoded;
  }

  co_return boost::leaf::success();
}

} // namespace feed
