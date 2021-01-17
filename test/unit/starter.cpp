#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#if defined(BOOST_NO_EXCEPTIONS)
namespace boost
{
void throw_exception([[maybe_unused]] const std::exception &) { std::abort(); }
} // namespace boost
#endif //  defined(BOOST_NO_EXCEPTIONS)

#if defined(ASIO_NO_EXCEPTIONS)
namespace asio::detail
{
template<typename exception_type>
void throw_exception(const exception_type &exception)
{
  boost::throw_exception(exception);
}
} // namespace asio::detail
#endif // defined(ASIO_NO_EXCEPTIONS)

#include "common/config_reader.hpp"
#include "common/properties_dispatch.hpp"
#include "model/automata.hpp"
#include "model/payload.hpp"
#include "trigger/trigger.hpp"
#include "trigger/trigger_dispatcher.hpp"
