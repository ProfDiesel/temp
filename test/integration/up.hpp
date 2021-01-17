#include "feed/feed_structures.hpp"
#include <variant>

namespace not_std
{
template<typename _Visitor, typename... _Variants>
constexpr decltype(auto) visit(_Visitor &&__visitor, _Variants &&...__variants)
{
  if((__variants.valueless_by_exception() || ...))
    std::__throw_bad_variant_access("Unexpected index");

  return std::__do_visit<false, true, decltype(__visitor), decltype(__variants)...>(std::forward<_Visitor>(__visitor), std::forward<_Variants>(__variants)...);
}
} // namespace not_std

namespace feed
{
using variant = std::variant<price_t, quantity_t>;
inline void poly_update_state(instrument_state &state, field field, const variant &value)
{
  not_std::visit([&](auto &&value) { /*update_state(state, field, value);*/ }, value);
}
} // namespace feed

struct wrapped_server : std::enable_shared_from_this<wrapped_server>
{
  std::mutex mutex;
  std::shared_ptr<asio::io_context> context;

  // serves snapshot requests
  auto run_forever()
  {
    auto thread = std::make_shared<std::thread>([thiz_ = std::shared_from_this(this)]() { thiz_->context->run(); });
    // lower the priority of the thread
    return thread;
  }

  // sync publish
  push_update(const std::vector<observer_ptr<instrument_state>> &states) { std::lock_gard _(mutex); }
};

