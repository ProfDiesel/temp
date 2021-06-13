#include "feed/feed_structures.hpp"

#include <memory>
#include <string_view>
#include <vector>

struct encoder;
struct up_server;

struct up_update_state
{
  feed::instrument_id_type instrument;
  feed::instrument_state state;
};

extern "C" up_encoder *up_encoder_new();
extern "C" void up_encoder_free(up_encoder *self);
extern "C" std::size_t up_encoder_encode(up_encoder *self, std::uint64_t timestamp, const up_update_state *states, std::size_t nb_states, std::byte *buffer,
                                         std::size_t buffer_size);

extern "C" up_server *up_server_new(const char *snapshot_host, const char *snapshot_service, const char *updates_host, const char *updates_service);
extern "C" void up_server_free(up_server *self);
extern "C" std::size_t up_server_poll(up_server *self);
extern "C" void up_server_push_update(up_server *self, const up_update_state *states, std::size_t nb_states);

// Hide coroutine code to Cling
namespace up
{

using update_state = ::up_update_state;

struct deleter
{
  void operator()(::up_encoder *ptr) noexcept { ::up_encoder_free(ptr); }
  void operator()(::up_server *ptr) noexcept { ::up_server_free(ptr); }
};

class encoder
{
public:
  encoder(): self(::up_encoder_new()) {}

  std::size_t encode(const std::uint64_t timestamp, const std::vector<::up_update_state> &states, std::byte *buffer, std::size_t buffer_size)
  { return ::up_encoder_encode(self.get(), states.data(), states.size(), buffer, buffer_size); }

private:
    std::unique_ptr<::up_encoder, deleter> self;
};

class server
{
public:
  server(std::string_view snapshot_address, std::string_view snapshot_service, std::string_view updates_address, std::string_view updates_service)
  : self(::up_server_new(snapshot_address.data(), snapshot_service.data(), updates_address.data(), updates_service.data())) {}

  std::size_t poll() { return ::up_server_poll(self.get()); }

  void push_update(const std::vector<::up_update_state> &states) { ::up_server_push_update(self.get(), states.data(), states.size()); }

private:
    std::unique_ptr<::up_server, deleter> self;
};

 auto make_server(const std::string &snapshot_address, const std::string &snapshot_service, const std::string &updates_address, const std::string &updates_service)
 {
   return server(snapshot_address, snapshot_service, updates_address, updates_service);
 }

}
