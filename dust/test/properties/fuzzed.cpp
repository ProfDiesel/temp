#include "feed/binary/feed_binary.hpp"
#include "handlers.hpp"
#include "model/wiring.hpp"

#include "backtest_harness.hpp"
#include "fuzz_test_harness.hpp"

#include <boilerplate/pointers.hpp>

#include <asio/defer.hpp>
#include <asio/io_context.hpp>
#include <asio/posix/stream_descriptor.hpp>

#include <boost/container/flat_map.hpp>
#include <boost/leaf/common.hpp>
#include <boost/leaf/handle_errors.hpp>

#include <std_function/function.h>

#include <chrono>
#include <functional>
#include <string_view>
#include <vector>

#ifndef __AFL_FUZZ_TESTCASE_LEN
ssize_t fuzz_len;
#  define __AFL_FUZZ_TESTCASE_LEN fuzz_len
unsigned char fuzz_buf[1024000];
#  define __AFL_FUZZ_TESTCASE_BUF fuzz_buf
#  define __AFL_FUZZ_INIT() void sync(void);
#  define __AFL_LOOP(x) ((fuzz_len = ::read(STDIN_FILENO, fuzz_buf, sizeof(fuzz_buf))) > 0 ? 1 : 0)
#  define __AFL_INIT() (::sync())
#endif

__AFL_FUZZ_INIT();

#if defined(BOOST_NO_EXCEPTIONS)
namespace boost
{
void throw_exception(const std::exception &exception) { std::abort(); }
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

using namespace std::chrono_literals;

namespace backtest
{

namespace detail
{
std::unique_ptr<deterministic_executor> executor_instance;
std::unique_ptr<buffer_feeder> feeder_instance;
} // namespace detail

snapshot_requester_type make_snapshot_requester() { return detail::feeder_instance->make_snapshot_requester(); }
update_source_type make_update_source() { return detail::feeder_instance->make_update_source(); }
send_stream_type make_stream_send() { return [](auto &&...){}; }

void delay([[maybe_unused]] asio::io_context &service, const std::chrono::steady_clock::duration &delay, delayed_action action)
{
  return detail::executor_instance->add(delay, action);
}

} // namespace backtest

auto main() -> int
{
  using namespace std::string_view_literals;
  using namespace logger::literals;

  backtest::detail::executor_instance.reset(new backtest::deterministic_executor {});

  asio::io_context service(1);
  asio::posix::stream_descriptor command_input(service), command_output(service);

  logger::printer printer {};
  logger::logger logger {boilerplate::make_strict_not_null(&printer)};

#ifdef __AFL_HAVE_MANUAL_CONTROL
  __AFL_INIT();
#endif

  const unsigned char *afl_buffer = __AFL_FUZZ_TESTCASE_BUF;

  while(__AFL_LOOP(10000))
  {
    const int afl_buffer_length = __AFL_FUZZ_TESTCASE_LEN;
    if(afl_buffer_length < 64)
      continue; // minimal usefull length

    std::vector mutable_buffer(afl_buffer, afl_buffer + afl_buffer_length);
    backtest::detail::feeder_instance.reset(new backtest::buffer_feeder(asio::buffer(mutable_buffer)));

    boost::leaf::try_handle_all(
      [&]() noexcept -> boost::leaf::result<void> {
        using namespace config::literals;

        const auto config = "\
\"config.type\" : \"dust\", \
\"config.feed\" : \"feed\", \
\"config.send\" : \"send\", \
\"config.command_out_fd\" : 1.0, \
\"config.subscription\" : \"subscription\", \
\"feed.snapshot\" : \"127.0.0.1:4400\", \
\"feed.update\" : \"239.255.0.1:4401\", \
\"feed.type\" : \"feed\", \
\"send.type\" : \"send\", \
\"send.fd\" : 14.0, \
\"send.disposable_payload\" : 0.0, \
\"send.cooldown\" : 2000000.0, \
\"subscription.type\" : \"subscription\", \
\"subscription.trigger\" : \"trigger\", \
\"subscription.payload\" : \"payload\", \
\"trigger.type\" : \"trigger\", \
\"trigger.instrument\" : 42.0, \
\"trigger.instant_threshold\" : 2.0, \
\"trigger.threshold\" : 3.0, \
\"trigger.period\" : 10.0, \
\"payload.type\" : \"payload\", \
\"payload.message\" : \"eyJpbnN0cnVtZW50IjogNDIsICJjb250ZW50IjogImhpdCAjMCJ9\", \
\"payload.datagram\" : \"eyJpbnN0cnVtZW50IjogNDIsICJjb250ZW50IjogImRhdGFncmFtIGhpdCAjMCJ9\" \
"sv;
        const auto properties = BOOST_LEAF_TRYX(config::properties::create(config));

        auto run = with_trigger_path(properties["config"_hs], service, command_input, command_output, boilerplate::make_strict_not_null(&logger),
                                     [&](auto fast_path) -> boost::leaf::result<void> {
                                       while(!service.stopped())
                                         [[likely]]
                                         {
                                           fast_path();
                                           backtest::detail::executor_instance->poll();
                                           if(service.poll())
                                             fuzz::bad_test(); // the non-deterministic executor is not supposed to be used
                                           logger.flush();
                                         }
                                       return {};
                                     });

        BOOST_LEAF_CHECK(run());
        return {};
      },
      make_handlers(std::ref(printer)));
    return 0;
  }
}
