#include <feed/feed_server.hpp>
#include <feed/feed_structures.hpp>
#include <feed/feedlibpp.hpp>

#include <boilerplate/leaf.hpp>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/ip/udp.hpp>
#include <asio/use_future.hpp>

#include <boost/range/iterator_range.hpp>

#include <cstring>
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

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

struct up_state
{
  up_instrument_id_t instrument;
  feed::instrument_state state;
};

extern "C" up_state *up_state_new(up_instrument_id_t instrument) { return new up_state {instrument}; }

///////////////////////////////////////////////////////////////////////////////
extern "C" void up_state_free(up_state *self) { delete self; }

///////////////////////////////////////////////////////////////////////////////
extern "C" void up_state_update_float(up_state *self, int8_t field, float value) { update_state_poly(self->state, static_cast<feed::field>(field), value); }

///////////////////////////////////////////////////////////////////////////////
extern "C" void up_state_update_uint(up_state *self, int8_t field, uint32_t value) { update_state_poly(self->state, static_cast<feed::field>(field), value); }

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

struct up_encoder
{
  feed::state_map state_map;
};

///////////////////////////////////////////////////////////////////////////////
extern "C" up_encoder *up_encoder_new() { return new up_encoder {}; }

///////////////////////////////////////////////////////////////////////////////
extern "C" void up_encoder_free(up_encoder *self) { delete self; }

///////////////////////////////////////////////////////////////////////////////
extern "C" std::size_t up_encoder_encode(up_encoder *self, up_timestamp_t timestamp, const up_state *const states[], std::size_t nb_states, void *buffer,
                                         std::size_t buffer_size)
{
  std::vector<std::tuple<feed::instrument_id_type, feed::instrument_state>> states_;
  states_.reserve(nb_states);
  std::for_each_n(states, nb_states, [&](const auto *state) { states_.emplace_back(state->instrument, state->state); });
  return self->state_map.update(states_,
                                [&](auto &&result)
                                {
                                  const auto size = result.size() + offsetof(feed::detail::event, packet);
                                  if(size < buffer_size)
                                  {
                                    auto *target = new(buffer) feed::detail::event {timestamp};
                                    std::memcpy(&target->packet, result.data(), result.size());
                                  }
                                  return size;
                                });
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

struct up_decoder
{
  std::function<void(std::uint8_t, float)> on_update_float;
  std::function<void(std::uint8_t, uint)> on_update_uint;
};

///////////////////////////////////////////////////////////////////////////////
extern "C" up_decoder *up_decoder_new(up_on_update_float_t on_update_float, up_on_update_uint_t on_update_uint) { return new up_decoder {on_update_float, on_update_uint}; }

///////////////////////////////////////////////////////////////////////////////
extern "C" void up_decoder_free(up_decoder *self) { delete self; }

///////////////////////////////////////////////////////////////////////////////
extern "C" std::size_t up_decoder_decode(up_decoder *self, const void *buffer, std::size_t buffer_size)
{
  return feed::decode([](auto instrument_id, auto sequence_id) { return instrument_id; },
                      [self](auto timestamp, auto update, auto instrument_id)
                      {
                        visit_update(
                          [self](auto field, auto value)
                          {
                            if constexpr(std::is_same_v<decltype(value), feed::price_t>)
                            {
                              self->on_update_float(static_cast<uint8_t>(field()), value);
                            }
                            else if constexpr(std::is_same_v<decltype(value), feed::quantity_t>)
                            {
                              self->on_update_uint(static_cast<uint8_t>(field()), value);
                            }
                            else
                            {
                              static_assert(boilerplate::always_false<decltype(value)>);
                            }
                          },
                          update);
                      },
                      network_clock::time_point(), asio::buffer(buffer, buffer_size));
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

struct up_future
{
  struct ok_t
  {
  };

  static constexpr auto ok_v = ok_t {};

  std::variant<std::nullopt_t, ok_t, std::string> value = std::nullopt;
};

///////////////////////////////////////////////////////////////////////////////
extern "C" void up_future_free(up_future *self) { delete self; }

///////////////////////////////////////////////////////////////////////////////
extern "C" bool up_future_is_set(const up_future *self) { return !std::holds_alternative<std::nullopt_t>(self->value); }

///////////////////////////////////////////////////////////////////////////////
extern "C" bool up_future_is_ok(const up_future *self) { return std::holds_alternative<up_future::ok_t>(self->value); }

///////////////////////////////////////////////////////////////////////////////
extern "C" const char *up_future_message(const up_future *self) { return std::get<std::string>(self->value).c_str(); }

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

namespace
{

auto make_handlers(up_future *future)
{
  return std::tuple {[future](const boost::leaf::e_source_location &location, const std::error_code &error_code)
                     {
                       std::ostringstream os;
                       os << location.file << ":" << location.line << " - error_code " << error_code.value() << ":" << error_code.message();
                       future->value = os.str();
                     },
                     [future](const boost::leaf::error_info &error_info)
                     {
                       std::ostringstream os;
                       os << error_info;
                       future->value = os.str();
                     }};
}

} // namespace

///////////////////////////////////////////////////////////////////////////////
struct up_server
{
  asio::io_context service;
  struct feed::server server;
  std::atomic_flag quit = false;

  up_server(std::string_view snapshot_host, std::string_view snapshot_service, std::string_view updates_host, std::string_view updates_service,
            up_future *future):
    server(service)
  {
    asio::co_spawn(
      service,
      [&]() noexcept -> boost::leaf::awaitable<void>
      {
        co_await boost::leaf::co_try_handle_all(
          [&]() noexcept -> boost::leaf::awaitable<boost::leaf::result<void>>
          {
            const auto snapshot_addresses
              = BOOST_LEAF_ASIO_CO_TRYX(co_await asio::ip::tcp::resolver(service).async_resolve(snapshot_host, snapshot_service, _));
            const auto updates_addresses = BOOST_LEAF_ASIO_CO_TRYX(co_await asio::ip::udp::resolver(service).async_resolve(updates_host, updates_service, _));
            BOOST_LEAF_CO_TRY(server.connect(*snapshot_addresses.begin(), *updates_addresses.begin()));
            while(!quit.test(std::memory_order_acquire))
              BOOST_LEAF_CO_TRY(co_await server.accept());
            future->value = up_future::ok_v;
            co_return boost::leaf::success();
          },
          make_handlers(future));
      },
      asio::detached);
  }

  ~up_server() { quit.notify_all(); }
};

///////////////////////////////////////////////////////////////////////////////
extern "C" up_server *up_server_new(const char *snapshot_host, const char *snapshot_service, const char *updates_host, const char *updates_service,
                                    up_future *future)
{
  return new up_server(snapshot_host, snapshot_service, updates_host, updates_service, future);
}

///////////////////////////////////////////////////////////////////////////////
extern "C" void up_server_free(up_server *self) { delete self; }

///////////////////////////////////////////////////////////////////////////////
extern "C" std::size_t up_server_poll(up_server *self) { return self->service.poll(); }

///////////////////////////////////////////////////////////////////////////////
extern "C" up_future *up_server_push_update(up_server *self, const up_state *const states[], std::size_t nb_states)
{
  std::vector<std::tuple<feed::instrument_id_type, feed::instrument_state>> states_;
  states_.reserve(nb_states);
  std::for_each_n(states, nb_states, [&](const auto *state) { states_.emplace_back(state->instrument, state->state); });
  auto *const result = new up_future();
  asio::co_spawn(
    self->service,
    [&, states = std::move(states_)]() noexcept -> boost::leaf::awaitable<void>
    { co_await boost::leaf::co_try_handle_all([&, states = std::move(states)]() { return self->server.update(states); }, make_handlers(result)); },
    asio::detached);
  return result;
}

///////////////////////////////////////////////////////////////////////////////
extern "C" up_future *up_server_replay(up_server *self, const void *buffer, std::size_t buffer_size)
{
  using clock_t = std::chrono::steady_clock;
  using timer_t = asio::basic_waitable_timer<clock_t>;

  auto *const result = new up_future();
  asio::co_spawn(
    self->service,
    [&]() noexcept -> boost::leaf::awaitable<void>
    {
      co_await boost::leaf::co_try_handle_all(
        [&]()
        {
          return feed::replay([self](auto states) { return self->server.update(std::forward<decltype(states)>(states)); },
                              [self, clock_0 = clock_t::now()](auto timestamp) mutable -> boost::leaf::awaitable<boost::leaf::result<void>>
                              {
                                BOOST_LEAF_ASIO_CO_TRY(co_await timer_t(self->service, timestamp).async_wait(_));
                                co_return boost::leaf::success();
                              },
                              asio::buffer(buffer, buffer_size));
        },
        make_handlers(result));
    },
    asio::detached);
  return result;
}
