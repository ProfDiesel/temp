#pragma once

#include <boost/leaf/context.hpp>
#include <boost/leaf/on_error.hpp>
#include <boost/leaf/result.hpp>

#include <outcome.hpp>

#define BOOST_LEAF_TRYX(...)                                                                                                                                   \
  ({                                                                                                                                                           \
    auto &&BOOST_LEAF_TOKEN_PASTE2(boost_leaf_temp_, __LINE__) = __VA_ARGS__;                                                                                  \
    if(!BOOST_LEAF_TOKEN_PASTE2(boost_leaf_temp_, __LINE__))                                                                                                   \
      return BOOST_LEAF_TOKEN_PASTE2(boost_leaf_temp_, __LINE__).error();                                                                                      \
    std::move(BOOST_LEAF_TOKEN_PASTE2(boost_leaf_temp_, __LINE__)).value();                                                                                    \
  })

#define BOOST_LEAF_EC_TRY(...)                                                                                                                                 \
  ({                                                                                                                                                           \
    std::error_code _;                                                                                                                                         \
    auto &&result = (__VA_ARGS__);                                                                                                                             \
    if(_) [[unlikely]]                                                                                                                                         \
      return BOOST_LEAF_NEW_ERROR(_);                                                                                                                          \
    std::forward<decltype(result)>(result);                                                                                                                    \
  })

#define BOOST_LEAF_TRY(...)                                                                                                                                    \
  if(auto _ = (__VA_ARGS__); _) [[unlikely]]                                                                                                                   \
    return std::error_code {_, std::generic_category()};

namespace boilerplate
{
namespace out = OUTCOME_V2_NAMESPACE;

template<typename try_block_type, typename handlers_type>
auto leaf_to_outcome(try_block_type &&try_block, handlers_type &&handlers) noexcept
  -> out::result<std::decay_t<decltype(*std::declval<try_block_type>()())>>
{
  auto context = std::apply([&](auto... handlers) { return boost::leaf::make_context(handlers...); }, handlers);
  auto active_context = boost::leaf::activate_context(context);
  if(auto result = std::forward<try_block_type>(try_block)())
    return std::move(result).value();
  else
  {
    auto id = result.error();
    context.deactivate();
    return out::failure(std::apply(
      [&](auto &&...handlers) { return context.template handle_error<std::error_code>(std::move(id), std::forward<decltype(handlers)>(handlers)...); },
      handlers));
  }
};
}
