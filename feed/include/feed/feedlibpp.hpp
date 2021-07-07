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

  void update(field_t field, float value) noexcept { ::up_state_update_float(self.get(), field, value); }
  void update(field_t field, std::uint32_t value) noexcept { ::up_state_update_uint(self.get(), field, value); }

  operator const ::up_state*() const noexcept { return self.get(); }

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
  { return ::up_encoder_encode(self.get(), timestamp, states.front(), states.size(), buffer, buffer_size); }

private:
    ptr<::up_encoder> self;
};


//
// decoder

class decoder
{
public:
  decoder() noexcept: self(::up_decoder_new()) {}

  std::size_t decode(timestamp_t timestamp, const states &states, void *buffer, std::size_t buffer_size) noexcept
  { return ::up_decoder_decode(self.get(), timestamp, states.front(), states.size(), buffer, buffer_size); }

private:
    ptr<::up_decoder> self;
};

//
// server

class server
{
public:
  server(std::string_view snapshot_address, std::string_view snapshot_service, std::string_view updates_address, std::string_view updates_service) noexcept
  : self(::up_server_new(snapshot_address.data(), snapshot_service.data(), updates_address.data(), updates_service.data())) {}

  std::size_t poll() noexcept { return ::up_server_poll(self.get()); }

  void push_update(const states &states) noexcept { ::up_server_push_update(self.get(), states.front(), states.size()); }

private:
    ptr<::up_server> self;
};

} // namespace up
