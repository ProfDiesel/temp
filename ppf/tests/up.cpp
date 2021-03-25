#include "up.hpp"

#include "feed/feed_server.hpp"
#include "feed/feed_structures.hpp"

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/ip/udp.hpp>

#include <iostream>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

///////////////////////////////////////////////////////////////////////////////
#if defined(BOOST_NO_EXCEPTIONS)
namespace boost
{
  void throw_exception(const std::exception &exception)
  {
    logger::printer printer;
    printer(0, logger::level::CRITICAL, exception.what());
    std::abort();
  }
} // namespace boost
#endif //  defined(BOOST_NO_EXCEPTIONS)

///////////////////////////////////////////////////////////////////////////////
#if defined(ASIO_NO_EXCEPTIONS)
namespace asio::detail
{
  template <typename exception_type>
  void throw_exception(const exception_type &exception)
  {
    boost::throw_exception(exception);
  }
} // namespace asio::detail
#endif // defined(ASIO_NO_EXCEPTIONS)

///////////////////////////////////////////////////////////////////////////////
struct up_server
{
  asio::io_context service;
  struct feed::server server;

  up_server(const asio::ip::tcp::endpoint &snapshot_address, const asio::ip::udp::endpoint &updates_address) : server(service, snapshot_address, updates_address)
  {
    asio::co_spawn(
        service, [this]() -> boost::leaf::awaitable<void> {
          for (;;)
            co_await server.accept();
          co_return;
        },
        asio::detached);
  }
};

///////////////////////////////////////////////////////////////////////////////
extern "C" up_server *up_server_new(const char *snapshot_host, const char *snapshot_service, const char *updates_host, const char *updates_service)
{
  asio::io_context service;
  const auto snapshot_addresses = asio::ip::tcp::resolver(service).resolve(snapshot_host, snapshot_service);
  const auto updates_addresses = asio::ip::udp::resolver(service).resolve(updates_host, updates_service);
  return new up_server(*snapshot_addresses.begin(), *updates_addresses.begin());
}

///////////////////////////////////////////////////////////////////////////////
extern "C" void up_server_free(up_server *self)
{
  delete self;
}

///////////////////////////////////////////////////////////////////////////////
extern "C" std::size_t up_server_poll(up_server *self)
{
  return self->service.poll();
}

///////////////////////////////////////////////////////////////////////////////
extern "C" void up_server_push_update(up_server *self, const up_update_state *states, std::size_t nb_states)
{
  std::vector<std::tuple<feed::instrument_id_type, feed::instrument_state>> states_;
  states_.reserve(nb_states);
  std::for_each_n(states, nb_states, [&](const auto &state) { states_.emplace_back(state.instrument, state.state); });
  asio::co_spawn(self->service, [&, states=states_]() { return self->server.update(states); }, asio::detached);
}

#if defined(TEST)
// GCOVR_EXCL_START
#include <boost/ut.hpp>

namespace ut = boost::ut;

ut::suite up_suite = [] {
  using namespace ut;

  "up_server"_test = [] {
    auto s = up::server("127.0.0.1", "9999", "127.0.0.1", "9998");
    auto n = s.poll();
  };
};

int main() {}

// GCOVR_EXCL_STOP
#endif // defined(TEST)
