#include <feed/feed_structures.hpp>

#include <string_view>
#include <functional>
#include <memory>

class polymorphic_trigger_dispatcher;
using trigger_ptr = std::shared_ptr<polymorphic_trigger_dispatcher>;

extern "C" trigger_ptr make_trigger(const std::string &config);
extern "C" bool run(trigger_ptr trigger, std::int64_t timestamp, feed::update update, const std::function<bool(std::int64_t , void *, bool)> &f);
