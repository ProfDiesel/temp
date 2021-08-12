#pragma once

#include <boost/leaf/error.hpp>

#include <asio/redirect_error.hpp>

// clang-format off
#define BOOST_LEAF_TRYV(...)                                                                                                                                   \
  {                                                                                                                                                            \
    if(auto _ = (__VA_ARGS__); _) [[unlikely]]                                                                                                                 \
      return std::error_code {_, std::generic_category()};                                                                                                     \
  }
// clang-format on

#define BOOST_LEAF_TRY(V, R) BOOST_LEAF_ASSIGN(V, R)

// clang-format off
#define BOOST_LEAF_TRYX(...)                                                                                                                                   \
  ({                                                                                                                                                           \
    auto &&BOOST_LEAF_TMP = (__VA_ARGS__);                                                                                                                     \
    if(!BOOST_LEAF_TMP)                                                                                                                                        \
      return BOOST_LEAF_TMP.error();                                                                                                                           \
    std::move(BOOST_LEAF_TMP).value();                                                                                                                         \
  })
// clang-format on

// clang-format off
#define BOOST_LEAF_CO_TRYV(...)                                                                                                                                \
  {                                                                                                                                                            \
    auto &&BOOST_LEAF_TMP = (__VA_ARGS__);                                                                                                                     \
    if(!BOOST_LEAF_TMP)                                                                                                                                        \
      co_return BOOST_LEAF_TMP.error();                                                                                                                        \
  }
// clang-format on

// clang-format off
#define BOOST_LEAF_CO_TRY(V, ...)                                                                                                                              \
  auto &&BOOST_LEAF_TMP = (__VA_ARGS__);                                                                                                                       \
  if(!BOOST_LEAF_TMP)                                                                                                                                          \
    co_return BOOST_LEAF_TMP.error();                                                                                                                          \
  V = std::forward<decltype(BOOST_LEAF_TMP)>(BOOST_LEAF_TMP).value()
// clang-format on

// clang-format off
#define BOOST_LEAF_CO_TRYX(...)                                                                                                                                \
  ({                                                                                                                                                           \
    auto &&BOOST_LEAF_TMP = (__VA_ARGS__);                                                                                                                     \
    if(!BOOST_LEAF_TMP)                                                                                                                                        \
      co_return BOOST_LEAF_TMP.error();                                                                                                                        \
    std::move(BOOST_LEAF_TMP).value();                                                                                                                         \
  })
// clang-format on

// clang-format off
#define BOOST_LEAF_EC_TRYV(...)                                                                                                                                \
  {                                                                                                                                                            \
    std::error_code _;                                                                                                                                         \
    (__VA_ARGS__);                                                                                                                                             \
    if(_) [[unlikely]]                                                                                                                                         \
      return BOOST_LEAF_NEW_ERROR(_).load(BOOST_PP_STRINGIZE(__VA_ARGS__));                                                                                    \
  }
// clang-format on

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
#define BOOST_LEAF_ASIO_CO_TRYV(...)                                                                                                                           \
  {                                                                                                                                                            \
    std::error_code ec;                                                                                                                                        \
    auto _ = asio::redirect_error(boost::leaf::use_awaitable, ec);                                                                                             \
    (__VA_ARGS__);                                                                                                                                             \
    if(ec) [[unlikely]]                                                                                                                                        \
      co_return BOOST_LEAF_NEW_ERROR(ec).load(BOOST_PP_STRINGIZE(__VA_ARGS__));                                                                                \
  }
// clang-format on

#define BOOST_LEAF_EC BOOST_LEAF_TOKEN_PASTE2(BOOST_LEAF_TMP, _ec)

// clang-format off
#define BOOST_LEAF_ASIO_CO_TRY2(V, TOKEN_NAME, ...)                                                                                                            \
  std::error_code BOOST_LEAF_EC;                                                                                                                               \
  auto TOKEN_NAME = asio::redirect_error(boost::leaf::use_awaitable, BOOST_LEAF_EC);                                                                           \
  auto &&BOOST_LEAF_TMP =  (__VA_ARGS__);                                                                                                                      \
  if(BOOST_LEAF_EC) [[unlikely]]                                                                                                                               \
    co_return BOOST_LEAF_NEW_ERROR(BOOST_LEAF_EC).load(BOOST_PP_STRINGIZE(__VA_ARGS__));                                                                       \
  V = std::forward<decltype(BOOST_LEAF_TMP)>(BOOST_LEAF_TMP);
// clang-format on

#define BOOST_LEAF_ASIO_CO_TRY(V, ...)  BOOST_LEAF_ASIO_CO_TRY2(V, _, __VA_ARGS__)

// clang-format off
#define BOOST_LEAF_ASIO_CO_TRYX(...)                                                                                                                           \
  ({                                                                                                                                                           \
    std::error_code ec;                                                                                                                                        \
    auto _ = asio::redirect_error(boost::leaf::use_awaitable, ec);                                                                                             \
    auto &&result = (__VA_ARGS__);                                                                                                                             \
    if(ec) [[unlikely]]                                                                                                                                        \
      co_return BOOST_LEAF_NEW_ERROR(ec).load(BOOST_PP_STRINGIZE(__VA_ARGS__));                                                                                \
    std::move(result);                                                                                                                                         \
  })
// clang-format on

// clang-format off
#define BOOST_LEAF_ERRNO_TRYX(EXPRESSION, CONDITION)                                                                                                           \
  ({                                                                                                                                                           \
    auto &&_ = (EXPRESSION);                                                                                                                                   \
    if(!(CONDITION)) [[unlikely]]                                                                                                                              \
      return BOOST_LEAF_NEW_ERROR().load(BOOST_PP_STRINGIZE(EXPRESSION));                                                                                      \
    std::move(_);                                                                                                                                              \
  })
// clang-format on

// clang-format off
#define BOOST_LEAF_RC_TRYV(...)                                                                                                                                \
  {                                                                                                                                                            \
    if(auto rc = (__VA_ARGS__); rc < 0) [[unlikely]]                                                                                                           \
      return BOOST_LEAF_NEW_ERROR(std::error_code(-rc, std::generic_category()), BOOST_PP_STRINGIZE(__VA_ARGS__));                                             \
  }
// clang-format on
