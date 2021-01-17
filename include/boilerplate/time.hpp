#pragma once

#include <boost/hof/infix.hpp>
#include <boost/hof/lift.hpp>
#include <type_traits>

namespace boilerplate
{
BOOST_HOF_STATIC_LAMBDA_FUNCTION(time) = boost::hof::infix([](const auto &left, const auto &right) {
  for(auto i = left; i; --i)
    right();
});

} // namespace boilerplate

