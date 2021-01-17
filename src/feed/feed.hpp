#pragma once

#if !defined(__USE_PREPROCESSED_FEED__HPP__)

#include <fmt/format.h>

#include "feed_structures.hpp"

#if defined(USE_FEED_TEXT)
#  include "feed_text.hpp"
#else
#  include "feed_binary.hpp"
#endif

template<feed::field field, typename char_type>
struct fmt::formatter<feed::field_c<field>, char_type> : fmt::formatter<const char *, char_type>
{
  template<typename context_type>
  auto format(feed::field_c<field> value, context_type &context)
  {
// clang-format off
#define HANDLE_FIELD(r, _, elem) if constexpr(value() == feed::field::BOOST_PP_TUPLE_ELEM(0, elem)) return format_to(context.out(), BOOST_PP_STRINGIZE(BOOST_PP_TUPLE_ELEM(0, elem)));
    BOOST_PP_SEQ_FOR_EACH(HANDLE_FIELD, _, FEED_FIELDS)
#undef HANDLE_FIELD
    // clang-format on
  }
};

#endif // !defined(__USE_PREPROCESSED_FEED__HPP__)
