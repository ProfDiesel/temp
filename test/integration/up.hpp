#include "feed/feed_structures.hpp"

#include <memory>
#include <string_view>
#include <vector>

struct up_server;

struct up_update_state
{
  feed::instrument_id_type instrument;
  feed::instrument_state state;
};

extern "C" up_server *up_server_new(const char *snapshot_address, const char *updates_address);
extern "C" void up_server_free(up_server *self);
extern "C" std::size_t up_server_poll(up_server *self);
extern "C" void up_server_push_update(up_server *self, const up_update_state *states, std::size_t nb_states);

// Hide coroutine code to Cling
namespace up 
{

struct deleter
{
  void operator()(up_server *ptr) noexcept { up_server_free(ptr); }
};

class server
{
public:
  server(std::string_view snapshot_address, std::string_view updates_address) noexcept : self(up_server_new(snapshot_address.data(), updates_address.data())) {}
  std::size_t poll() noexcept { return up_server_poll(self.get()); }
  void push_update(const std::vector<up_update_state> &states) noexcept { up_server_push_update(self.get(), states.data(), states.size()); }

private:
    std::unique_ptr<up_server, deleter> self;
};
}