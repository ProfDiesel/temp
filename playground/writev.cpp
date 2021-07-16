#include <asio/connect.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/write.hpp>

#include <chrono>
#include <cstddef>
#include <iostream>
#include <string>
#include <thread>

#include <sys/uio.h>

namespace asio::detail
{
template<typename exception_type>
void throw_exception(const exception_type &exception)
{
  std::abort();
}
} // namespace asio::detail

int main()
{
  std::error_code ec;

  asio::io_context service(1);

  const auto endpoints = asio::ip::tcp::resolver(service).resolve("127.0.0.1", "4444", ec);
  auto socket = asio::ip::tcp::socket(service);
  asio::connect(socket, endpoints, ec);
  std::clog << "connected " << socket.native_handle() << std::endl;

  if(::fork())
  {
    std::string content(1'024 * 1'024 * 128, 'a');
    asio::write(socket, asio::buffer(content));

    using namespace std::chrono_literals;
    std::this_thread::sleep_for(10s);
  }
  else
  {
    if(::fork())
    {
      std::string content(1'024 * 1'024 * 128, 'b');
      asio::write(socket, asio::buffer(content));
    }
    else
    {
      std::string content(1'024 * 1'024 * 128, 'c');
      asio::write(socket, asio::buffer(content));
    }
    socket.release();
  }

  return 0;
}

// socat -u TCP-LISTEN:4444,reuseaddr OPEN:out.txt,creat
