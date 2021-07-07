#pragma once

#include <papipp.h>

#include <boost/hof/decorate.hpp>

namespace detail
{
template<papi::event_code... codes>
struct with_counters
{
  using event_set = ::papi::event_set<codes...>;

  template<typename function_type, typename... args_types>
  auto operator()(const event_set &events, function_type &&f, args_types &&... args) const noexcept -> decltype(f(std::forward<args_types>(args)...))
  {
    struct scoped_events
    {
      event_set &events;
      scoped_events() { events.start_counters(); }
      ~scoped_events() { events.stop_counters(); }
    } _ {events};
    return f(std::forward<args_types>(args)...);
  }
};
} // namespace detail

BOOST_HOF_STATIC_FUNCTION(with_counters) = boost::hof::decorate(detail::with_counters<PAPI_TOT_INS, PAPI_TOT_CYC>());

