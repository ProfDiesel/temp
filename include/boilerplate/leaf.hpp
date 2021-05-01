#pragma once

#include <boost/leaf/as_result.hpp>
#include <boost/leaf/context.hpp>
#include <boost/leaf/coro.hpp>
#include <boost/leaf/on_error.hpp>
#include <boost/leaf/result.hpp>

#define BOOST_LEAF_TRY(...)                                                                                                                                    \
  {                                                                                                                                                            \
    if(auto _ = (__VA_ARGS__); _) [[unlikely]]                                                                                                                 \
      return std::error_code {_, std::generic_category()};                                                                                                     \
  }

// clang-format off
#define BOOST_LEAF_TRYX(...)                                                                                                                                   \
  ({                                                                                                                                                           \
    auto &&BOOST_LEAF_TMP = (__VA_ARGS__);                                                                                                                     \
    if(!BOOST_LEAF_TMP)                                                                                                                                        \
      return BOOST_LEAF_TMP.error();                                                                                                                           \
    std::move(BOOST_LEAF_TMP).value();                                                                                                                         \
  })
// clang-format on

#define BOOST_LEAF_CO_TRY(...)                                                                                                                                 \
  {                                                                                                                                                            \
    auto &&BOOST_LEAF_TMP = (__VA_ARGS__);                                                                                                                     \
    if(!BOOST_LEAF_TMP)                                                                                                                                        \
      co_return BOOST_LEAF_TMP.error();                                                                                                                        \
  }

// clang-format off
#define BOOST_LEAF_CO_TRYX(...)                                                                                                                                \
  ({                                                                                                                                                           \
    auto &&BOOST_LEAF_TMP = (__VA_ARGS__);                                                                                                                     \
    if(!BOOST_LEAF_TMP)                                                                                                                                        \
      co_return BOOST_LEAF_TMP.error();                                                                                                                        \
    std::move(BOOST_LEAF_TMP).value();                                                                                                                         \
  })
// clang-format on

#define BOOST_LEAF_EC_TRY(...)                                                                                                                                 \
  {                                                                                                                                                            \
    std::error_code _;                                                                                                                                         \
    (__VA_ARGS__);                                                                                                                                             \
    if(_) [[unlikely]]                                                                                                                                         \
      return BOOST_LEAF_NEW_ERROR(_).load(BOOST_PP_STRINGIZE(__VA_ARGS__));                                                                                    \
  }

// clang-format off
#define BOOST_LEAF_EC_TRYX(...)                                                                                                                                \
  ({                                                                                                                                                           \
    std::error_code _;                                                                                                                                         \
    auto &&result = (__VA_ARGS__);                                                                                                                             \
    if(_) [[unlikely]]                                                                                                                                         \
      return BOOST_LEAF_NEW_ERROR(_).load(BOOST_PP_STRINGIZE(__VA_ARGS__));                                                                                    \
    std::move(result);                                                                                                                                         \
  })
// clang-format on

// clang-format off
#define BOOST_LEAF_EC_CO_TRYX(...)                                                                                                                             \
  ({                                                                                                                                                           \
    std::error_code ec;                                                                                                                                        \
    auto &&handler = asio::redirect_error(boost::leaf::use_awaitable, ec);                                                                                            \
    auto &&result = (__VA_ARGS__);                                                                                                                             \
    if(_) [[unlikely]]                                                                                                                                         \
      co_return BOOST_LEAF_NEW_ERROR(ec).load(BOOST_PP_STRINGIZE(__VA_ARGS__));                                                                                \
    std::move(result);                                                                                                                                         \
  })
// clang-format on

// clang-format off
#define BOOST_LEAF_ERRNO_TRYX(EXPRESSION, CONDITION)                                                                                                           \
  ({                                                                                                                                                           \
    auto &&_ = (EXPRESSION);                                                                                                                                   \
    if(!(CONDITION)) [[unlikely]]                                                                                                                              \
      return BOOST_LEAF_NEW_ERROR().load(BOOST_PP_STRINGIZE(__VA_ARGS__));                                                                                     \
    std::move(_);                                                                                                                                              \
  })
// clang-format on

#define BOOST_LEAF_RC_TRY(...)                                                                                                                                 \
  {                                                                                                                                                            \
    if(auto rc = (__VA_ARGS__); rc < 0) [[unlikely]]                                                                                                           \
      return BOOST_LEAF_NEW_ERROR(std::error_code(-rc, std::generic_category()), BOOST_PP_STRINGIZE(__VA_ARGS__));                                             \
  }
// clang-format on

