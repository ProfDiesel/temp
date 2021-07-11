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
  boilerplate::not_null_observer_ptr<server> server;

  session(asio::ip::tcp::socket &&socket, boilerplate::not_null_observer_ptr<struct server> server) noexcept: socket(std::move(socket)), server(server) {}

  boost::leaf::awaitable<boost::leaf::result<void>> operator()() noexcept;
};
} // namespace detail

class server : public state_map
{
public:
  server(asio::io_context &service) noexcept: service(service), updates_socket(service), snapshot_acceptor(service) {}

  boost::leaf::result<void> connect(const asio::ip::tcp::endpoint &snapshot_endpoint, const asio::ip::udp::endpoint &updates_endpoint) noexcept
  {
    this->updates_endpoint = updates_endpoint;

    BOOST_LEAF_EC_TRY(snapshot_acceptor.open(snapshot_endpoint.protocol(), _));
    BOOST_LEAF_EC_TRY(snapshot_acceptor.set_option(asio::ip::tcp::socket::reuse_address(true), _));
    BOOST_LEAF_EC_TRY(snapshot_acceptor.bind(snapshot_endpoint, _));
    BOOST_LEAF_EC_TRY(snapshot_acceptor.listen(asio::ip::tcp::socket::max_listen_connections, _));

    return boost::leaf::success();
  }

  boost::leaf::awaitable<boost::leaf::result<void>>
  update(const auto &states) noexcept // TODO requires is_iterable<decltype(states), std::tuple<instrument_id_type, instrument_state>>
  {
    BOOST_LEAF_ASIO_CO_TRY(co_await state_map::update(states, [&](auto &&buffer) { return updates_socket.async_send_to(buffer, updates_endpoint, _); }));
    co_return boost::leaf::success();
  }

  instrument_state snapshot(instrument_id_type instrument) const noexcept { return at(instrument); }

  boost::leaf::awaitable<boost::leaf::result<void>> accept() noexcept
  {
    asio::ip::tcp::socket socket(service);
    BOOST_LEAF_ASIO_CO_TRY(co_await snapshot_acceptor.async_accept(socket, _));
    auto session_ptr = std::make_shared<detail::session>(std::move(socket), boilerplate::make_strict_not_null(this));
    asio::co_spawn(
      service,
      [&]() noexcept -> boost::leaf::awaitable<void>
      {
        auto result = co_await(*session_ptr)();
        if(!result)
        {
          // TODO : add logger
          using namespace logger::literals;
          // logger->log(logger::critical, "{}."_format, result.assume_error());
        }
      },
      asio::detached);

    co_return boost::leaf::success();
  }

private:
  asio::io_context &service;
  asio::ip::udp::socket updates_socket;
  asio::ip::udp::endpoint updates_endpoint;
  asio::ip::tcp::acceptor snapshot_acceptor;
};

inline boost::leaf::awaitable<boost::leaf::result<void>> detail::session::operator()() noexcept
{
  const auto self(shared_from_this());

  detail::snapshot_request request;
  if(BOOST_LEAF_ASIO_CO_TRYX(co_await asio::async_read(socket, asio::buffer(&request, sizeof(request)), _)) != sizeof(request))
    co_return std::make_error_code(std::errc::io_error); // TODO

  std::array<char, 128> buffer;
  const auto actual_length = feed::detail::encode_message(request.instrument.value(), server->snapshot(request.instrument.value()),
                                                          asio::mutable_buffer(buffer.data(), buffer.size()));
  if(BOOST_LEAF_ASIO_CO_TRYX(co_await asio::async_write(socket, asio::buffer(buffer.data(), actual_length), _)) != actual_length)
    co_return std::make_error_code(std::errc::io_error); // TODO

  co_return boost::leaf::success();
}

boost::leaf::awaitable<boost::leaf::result<void>> replay(auto &&co_continuation, auto &&co_wait_until, asio::const_buffer buffer)
{
  const auto *current = reinterpret_cast<const feed::detail::event *>(buffer.data());
  const auto timestamp_0 = network_clock::time_point(std::chrono::nanoseconds(current->timestamp));

  boost::container::flat_map<instrument_id_type, instrument_state> states;
  while(buffer.size() > sizeof(feed::detail::event))
  {
    const auto *current = reinterpret_cast<const feed::detail::event *>(buffer.data());
    const auto timestamp = network_clock::time_point(std::chrono::nanoseconds(current->timestamp));
    BOOST_LEAF_ASIO_CO_TRY(co_await co_wait_until(timestamp - timestamp_0));
    buffer += offsetof(feed::detail::event, packet);

    states.clear();
    const auto decoded = feed::detail::decode([](auto instrument_id, auto sequence_id) { return instrument_id; },
                                              [&states](const auto &timestamp, const auto &update, const auto &instrument_closure)
                                              { update_state(states[instrument_closure], update); },
                                              timestamp, buffer);

    BOOST_LEAF_CO_TRY(co_await co_continuation(states));

    buffer += decoded;
  }

  co_return boost::leaf::success();
}

} // namespace feed
