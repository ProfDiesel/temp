#pragma once

#include <cassert>

#include <boilerplate/boilerplate.hpp> // for LIKELY

#include <boost/preprocessor/cat.hpp>

#include <gsl/util>

#if !defined(NDEBUG)
#define ASSERTS(predicate) assert(LIKELY(predicate));
#define REQUIRES(predicate) ASSERTS(predicate);
#define ENSURES(predicate) auto BOOST_PP_CAT(__boilerplate_ensures_, __LINE__) = gsl::finally([&]() { assert(LIKELY(predicate)); });
#else // !defined(NDEBUG)
#define ASSERTS(predicate) ;
#define REQUIRES(predicate) ;
#define ENSURES(predicate) ;
#endif // !defined(NDEBUG)
