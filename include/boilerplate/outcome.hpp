#include <outcome.hpp>
#include <outcome/as_result.hpp>

namespace out = OUTCOME_V2_NAMESPACE;

#define OUTCOME_EC_TRYX(...)                                                                                                                                   \
  ({                                                                                                                                                           \
    std::error_code _;                                                                                                                                         \
    auto &&result = (__VA_ARGS__);                                                                                                                             \
    if(_) [[unlikely]]                                                                                                                                         \
      return _;                                                                                                                                                \
    std::forward<decltype(result)>(result);                                                                                                                    \
  })

#define OUTCOME_EC_TRYV(...)                                                                                                                                   \
  ({                                                                                                                                                           \
    std::error_code _;                                                                                                                                         \
    __VA_ARGS__;                                                                                                                                               \
    if(_) [[unlikely]]                                                                                                                                         \
      return _;                                                                                                                                                \
  })

#define OUTCOME_EC_CORO_TRYX(...)                                                                                                                              \
  ({                                                                                                                                                           \
    std::error_code ec;                                                                                                                                        \
    auto &&handler = asio::redirect_error(asio::use_awaitable, ec);                                                                                            \
    auto &&result = co_await(__VA_ARGS__);                                                                                                                     \
    if(_) [[unlikely]]                                                                                                                                         \
      co_return _;                                                                                                                                             \
    std::forward<decltype(result)>(result);                                                                                                                    \
  })
