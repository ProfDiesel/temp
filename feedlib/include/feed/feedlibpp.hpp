#pragma once

#include <feed/feedlib.h>

#include <memory>
#include <string_view>
#include <vector>


// Hide coroutine code to Cling
namespace up
{

struct deleter
{
  void operator()(::up_state *ptr) noexcept { ::up_state_free(ptr); }
  void operator()(::up_encoder *ptr) noexcept { ::up_encoder_free(ptr); }
  void operator()(::up_decoder *ptr) noexcept { ::up_decoder_free(ptr); }
  void operator()(::up_future *ptr) noexcept { ::up_future_free(ptr); }
  void operator()(::up_server *ptr) noexcept { ::up_server_free(ptr); }
};

template<typename value_type>
using ptr = std::unique_ptr<value_type, deleter>;

//
// state

using instrument_id_t = ::up_instrument_id_t;
using field_t = ::up_field_t;
using timestamp_t = ::up_timestamp_t;

class state
{
public:
  explicit state(instrument_id_t instrument) noexcept : self(::up_state_new(instrument)) {}

  auto get_float(field_t field) const noexcept { return ::up_state_get_float(self.get(), field); }
  auto get_uint(field_t field) const noexcept { return ::up_state_get_uint(self.get(), field); }

  void update_float(field_t field, float value) noexcept { ::up_state_update_float(self.get(), field, value); }
  void update_uint(field_t field, std::uint32_t value) noexcept { ::up_state_update_uint(self.get(), field, value); }

  auto get() noexcept { return self.get(); }
  auto get() const noexcept { return self.get(); }

private:
    ptr<::up_state> self;
};
static_assert(sizeof(state) == sizeof(::up_state*));

using states = std::vector<state>;

//
// encoder

class encoder
{
public:
  encoder() noexcept: self(::up_encoder_new()) {}

  std::size_t encode(timestamp_t timestamp, const states &states, void *buffer, std::size_t buffer_size) noexcept
  { return ::up_encoder_encode(self.get(), timestamp, reinterpret_cast<const ::up_state* const*>(states.data()), states.size(), buffer, buffer_size); }

private:
    ptr<::up_encoder> self;
};


//
// decoder

using on_update_float_t = up_on_update_float_t;
using on_update_uint_t = up_on_update_uint_t;

class decoder
{
public:
  decoder(on_update_float_t on_update_float, on_update_uint_t on_update_uint) noexcept: self(::up_decoder_new(on_update_float, on_update_uint)) {}

  std::size_t decode(const void *buffer, std::size_t buffer_size) noexcept { return ::up_decoder_decode(self.get(), buffer, buffer_size); }

private:
    ptr<::up_decoder> self;
};

//
// server

class future
{
public:
  bool is_set() const noexcept { return ::up_future_is_set(self.get()); }
  operator bool() const noexcept { return ::up_future_is_ok(self.get()); }
  std::string_view message() const noexcept { return ::up_future_message(self.get()); }

  static auto wrap(::up_future *ptr) noexcept { return future(ptr); }
  auto get() noexcept { return self.get(); }

private:
    ptr<::up_future> self;

    explicit future(::up_future *ptr): self(ptr) {}
};

class server
{
public:
  server(std::string_view snapshot_address, std::string_view snapshot_service, std::string_view updates_address, std::string_view updates_service, future &future) noexcept
  : self(::up_server_new(snapshot_address.data(), snapshot_service.data(), updates_address.data(), updates_service.data(), future.get())) {}

  std::size_t poll() noexcept { return ::up_server_poll(self.get()); }

  future push_update(const states &states) noexcept { return future::wrap(::up_server_push_update(self.get(), reinterpret_cast<const ::up_state* const*>(states.data()), states.size())); }
  future replay(const void *buffer, std::size_t buffer_size) noexcept { return future::wrap(::up_server_replay(self.get(), buffer, buffer_size)); }

private:
    ptr<::up_server> self;
};

} // namespace up
