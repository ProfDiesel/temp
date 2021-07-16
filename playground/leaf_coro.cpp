#include <asio.hpp>
#include <boost/leaf.hpp>
#include <boost/leaf/coro.hpp>
#include <iostream>

#if defined(BOOST_NO_EXCEPTIONS)
namespace boost
{
/*[[noreturn]]*/ void throw_exception(const std::exception &exception) { std::abort(); }

struct source_location;
[[noreturn]] void throw_exception(std::exception const &e, boost::source_location const &) { throw_exception(e); }
} // namespace boost

#endif //  defined(BOOST_NO_EXCEPTIONS)

#if defined(ASIO_NO_EXCEPTIONS)
namespace asio::detail
{
template<typename exception_type>
/*[[noreturn]]*/ void throw_exception(const exception_type &exception)
{
  boost::throw_exception(exception);
}
} // namespace asio::detail
#endif // defined(ASIO_NO_EXCEPTIONS)

namespace bl = boost::leaf;

asio::io_context io_context;

const auto _ = __TIME__;

int main()
{
  asio::io_context io_context;

  asio::co_spawn(
    io_context,
    [&]() noexcept -> bl::awaitable<void> {
      std::clog << "try 1\n";
      co_await bl::co_try_handle_all(
        [&]() noexcept -> bl::awaitable<bl::result<void>> {
          asio::steady_timer timer(io_context);
          timer.expires_after(std::chrono::milliseconds(500));
          std::clog << "before wait 1\n";
          co_await timer.async_wait(bl::use_awaitable);
          std::clog << "after wait 1\n";
          bl::result<void> result = bl::new_error(42);
          co_return result;
        },
        [](int i) { std::clog << "error 1 " << i << "\n"; }, [](const bl::error_info &ei, int i) { std::clog << "error 1 " << ei << " " << i << "\n"; },
        [](const bl::error_info &ei) { std::clog << "error 1 " << ei << "\n"; });
      std::clog << "return 1\n";
      co_return;
    },
    asio::detached);
  asio::co_spawn(
    io_context,
    [&]() noexcept -> bl::awaitable<void> {
      std::clog << "try 2\n";
      co_await bl::co_try_handle_all(
        [&]() noexcept -> bl::awaitable<bl::result<void>> {
          asio::steady_timer timer(io_context);
          auto result = bl::new_error(43);
          timer.expires_after(std::chrono::seconds(1));
          std::clog << "before wait 2\n";
          co_await timer.async_wait(bl::use_awaitable);
          std::clog << "after wait 2\n";
          co_return result;
        },
        [](int i) { std::clog << "error 2 " << i << "\n"; }, [](const bl::error_info &ei, int i) { std::clog << "error 2 " << ei << " " << i << "\n"; },
        [](const bl::error_info &ei) { std::clog << "error 2 " << ei << "\n"; });
      std::clog << "return 2\n";
      co_return;
    },
    asio::detached);

  io_context.run();
}
