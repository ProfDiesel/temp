#include "up.hpp"

#include "boilerplate/leaf.hpp"

#include "feed/feed_server.hpp"
#include "feed/feed_structures.hpp"

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/ip/udp.hpp>
#include <asio/use_future.hpp>

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
template<typename exception_type>
void throw_exception(const exception_type &exception)
{
  boost::throw_exception(exception);
}
} // namespace asio::detail
#endif // defined(ASIO_NO_EXCEPTIONS)


struct up_encoder
{
  up_encoder(){}
  ~up_encoder(){}
};

///////////////////////////////////////////////////////////////////////////////
extern "C" up_encoder *up_encoder_new()
{

}

///////////////////////////////////////////////////////////////////////////////
extern "C" void up_encoder_free(up_encoder *self) { delete self; }

///////////////////////////////////////////////////////////////////////////////
extern "C" std::size_t up_encoder_encode(up_encoder *self, std::uint64_t timestamp, const up_update_state *states, std::size_t nb_states, std::byte *buffer,
                                         std::size_t buffer_size)
{

}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

const auto handlers = std::tuple {[](const boost::leaf::e_source_location &location, const std::error_code &error_code) -> boost::leaf::awaitable<void> {
                                    std::clog << location.file << ":" << location.line << " - error_code " << error_code.value() << ":" << error_code.message()
                                              << "\n";
                                    std::abort();
                                  },
                                  [](const boost::leaf::error_info &error_info) -> boost::leaf::awaitable<void> { std::abort(); }};

///////////////////////////////////////////////////////////////////////////////
struct up_server
{
  asio::io_context service;
  struct feed::server server;
  std::atomic_flag quit = false;

  up_server(const asio::ip::tcp::endpoint &snapshot_address, const asio::ip::udp::endpoint &updates_address): server(service)
  {
    boost::leaf::try_handle_all([&]() noexcept { return server.connect(snapshot_address, updates_address); }, handlers);

    asio::co_spawn(
      service,
      [&]() noexcept -> boost::leaf::awaitable<void> {
        co_await boost::leaf::co_try_handle_all(
          [&]() noexcept -> boost::leaf::awaitable<boost::leaf::result<void>> {
            while(!quit.test(std::memory_order_acquire))
              BOOST_LEAF_CO_TRY(co_await server.accept());
            co_return boost::leaf::success();
          },
          handlers);
      },
      asio::detached);
  }

  ~up_server() { quit.notify_all(); }
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
extern "C" void up_server_free(up_server *self) { delete self; }

///////////////////////////////////////////////////////////////////////////////
extern "C" std::size_t up_server_poll(up_server *self) { return self->service.poll(); }

///////////////////////////////////////////////////////////////////////////////
extern "C" void up_server_push_update(up_server *self, const up_update_state *states, std::size_t nb_states)
{
  std::vector<std::tuple<feed::instrument_id_type, feed::instrument_state>> states_;
  states_.reserve(nb_states);
  std::for_each_n(states, nb_states, [&](const auto &state) { states_.emplace_back(state.instrument, state.state); });
  asio::co_spawn(
    self->service,
    [&, states = std::move(states_)]() noexcept -> boost::leaf::awaitable<void> {
      co_await boost::leaf::co_try_handle_all([&, states = std::move(states)]() { return self->server.update(states); }, handlers);
    },
    asio::detached);
}

#if defined(TEST)
// GCOVR_EXCL_START
#  include <boost/ut.hpp>

namespace ut = boost::ut;

ut::suite up_suite = [] {
  using namespace ut;

  "up_server"_test = [] {
    auto *s = up_server_new("127.0.0.1", "4400", "127.0.0.1", "4401");
    up_server_poll(s);

    feed::instrument_state instrument_state;
    feed::update_state_poly(instrument_state, feed::field::b0, 10.0);
    feed::update_state_poly(instrument_state, feed::field::bq0, 1);
    instrument_state.sequence_id = 5;

    up_update_state state {42, instrument_state};
    up_server_push_update(s, &state, 1);

    for(int i = 0; i < 100; ++i)
      up_server_poll(s);

    up_server_free(s);
  };
};

int main() {}
// GCOVR_EXCL_STOP
#endif // defined(TEST)
