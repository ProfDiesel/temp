#pragma once

#include <boost/config.hpp>
#include <boost/range/iterator_range.hpp>
#include <boost/throw_exception.hpp>
#include <boost/type_index.hpp>
#define typeid(x) boost::typeindex::type_id<x>()
#include <boost/spirit/home/x3.hpp>
#undef typeid

namespace x3_ext
{
template<typename iterator_type>
struct expectation_failure
{
  std::string which;
  boost::iterator_range<iterator_type> where;
};
template<typename iterator_type>
expectation_failure(std::string_view, const boost::iterator_range<iterator_type> &) -> expectation_failure<iterator_type>;

template<typename subject_type>
auto expect(subject_type &&subject)
{
  namespace x3 = boost::spirit::x3;
  const auto parser = x3::as_parser(std::forward<subject_type>(subject));

  const auto store = [](auto &context) { x3::_val(context) = x3::_attr(context); };
  const auto error = [which = x3::what(parser)](auto &context) {
    BOOST_LEAF_NEW_ERROR(expectation_failure {which, x3::_where(context)});
    x3::_pass(context) = false;
  };
  return x3::rule<struct _, typename std::decay_t<decltype(parser)>::attribute_type> {"expect"} = parser[store] | x3::eps[error];
}

} // namespace x3_ext
