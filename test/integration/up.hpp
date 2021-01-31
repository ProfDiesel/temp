#include "feed/feed_structures.hpp"
#include "boilerplate/pointers.hpp"

#include <asio/io_context.hpp>

#include <mutex>
#include <variant>

namespace feed
{

class wrapped_server : public std::enable_shared_from_this<wrapped_server>
{
public:
  // serves snapshot requests
  auto run_forever()
  {
    auto thread = std::make_shared<std::thread>([thiz_ = shared_from_this()]() { thiz_->context->run(); });
    // lower the priority of the thread
    return thread;
  }

  // sync publication
  void push_update(const std::vector<boilerplate::observer_ptr<instrument_state>> &states) { std::lock_guard _(mutex); }

private:
  std::mutex mutex;
  std::shared_ptr<asio::io_context> context;
};

inline auto make_wrapped_server() { return std::make_shared<wrapped_server>(); }

} // namespace feed

