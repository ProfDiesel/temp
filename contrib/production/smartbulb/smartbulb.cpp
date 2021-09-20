#include <boost/container/flat_set.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/process/spawn.hpp>
#include <boost/stacktrace.hpp>

#include <cstdlib>
#include <iostream>
#include <iterator>
#include <numeric>
#include <regex>

#include <arpa/inet.h>
#include <dlfcn.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#if !defined(__clang__)
#include <ext/stdio_filebuf.h>
#endif // !defined(__clang__)

namespace
{
auto safe_getenv(std::string_view name) noexcept
{
  const auto value = std::getenv(name.data());
  return value ? value : "";
}

#if defined(DEBUG)
bool verbose = false;
#  define SMARTBULB_LOG(expression)                                                                                                                            \
    if(verbose) [[unlikely]]                                                                                                                                   \
    {                                                                                                                                                          \
      std::clog << expression << '\n';                                                                                                                         \
    }
#else
#  define SMARTBULB_LOG()
#endif // defined(DEBUG)

#if !defined(__clang__)
__gnu_cxx::stdio_filebuf<char> log_filebuf;
#endif // !defined(__clang__)

std::optional<std::regex> stack_re = {};
std::optional<std::regex> address_re = {};
std::string_view command_line = {};

decltype(&socket) default_socket = {}, nonaccel_socket = {};
decltype(&connect) default_connect = {};
decltype(&write) default_write = {};
decltype(&sendmsg) default_sendmsg = {};
decltype(&close) default_close = {};

boost::container::flat_set<int> managed_sockets;

// __attribute__((constructor(65535)))
void init() noexcept
{
#if defined(DEBUG)
  verbose = boost::lexical_cast<int>(safe_getenv("SMARTBULB_VERBOSE"));
#endif // defined(DEBUG)

#if !defined(__clang__)
  const auto log_fd = boost::lexical_cast<int>(safe_getenv("SMARTBULB_SMARTBULB_LOG_FD"));
  if(log_fd)
  {
    log_filebuf = __gnu_cxx::stdio_filebuf<char>(log_fd, std::ios::out);
    std::clog.rdbuf(&log_filebuf);
  }
#endif // !defined(__clang__)

  try
  {
    const auto stack_re_str = safe_getenv("SMARTBULB_STACK_RE");
    if(stack_re_str)
      stack_re = stack_re_str;
    const auto address_re_str = safe_getenv("SMARTBULB_ADDRESS_RE");
    if(address_re_str)
      address_re = address_re_str;
  }
  catch(const std::regex_error &error)
  {
    std::clog << error.what() << '\n';
  }

  command_line = safe_getenv("SMARTBULB_COMMAND_LINE");

  const auto load_symbol = [&](auto name, auto &ptr) {
    ptr = reinterpret_cast<std::decay_t<decltype(ptr)>>(::dlsym(RTLD_NEXT, name));
    SMARTBULB_LOG(std::hex << reinterpret_cast<const void *>(ptr) << " replaced by " << ::dlsym(RTLD_DEFAULT, name) << std::dec);
  };

  load_symbol("socket", default_socket);
  load_symbol("onload_socket_nonaccel", nonaccel_socket);
  load_symbol("connect", default_connect);
  load_symbol("write", default_write);
  load_symbol("sendmsg", default_sendmsg);
  load_symbol("close", default_close);
}

static struct _init
{
  _init() { init(); }
} _;

} // namespace

extern "C" __attribute__((visibility("default"))) int socket(int domain, int type, int protocol)
{
  auto socket = default_socket;
  if(nonaccel_socket)
  {
    const auto stacktrace = boost::stacktrace::stacktrace();
    const auto stacktrace_as_string = std::accumulate(std::next(stacktrace.begin()), stacktrace.end(), std::string(), [](auto &&acc, const auto &frame) {
      return !frame.name().empty() ? std::move(acc) + '\n' + frame.name() : acc;
    });

    SMARTBULB_LOG("socket(): \n" << stacktrace_as_string);

    if(stack_re && !std::regex_match(stacktrace_as_string, *stack_re))
      socket = nonaccel_socket;
  }
  return (*socket)(domain, type, protocol);
}

extern "C" __attribute__((visibility("default"))) int connect(int fd, const struct sockaddr *address, socklen_t address_len)
{
  const auto result = (*default_connect)(fd, address, address_len);

  char address_as_string[64] = {0};
  switch(address->sa_family)
  {
  case AF_INET:
    ::inet_ntop(AF_INET, &(((struct sockaddr_in *)address)->sin_addr), address_as_string, sizeof(address_as_string));
    std::snprintf(address_as_string + std::strlen(address_as_string), sizeof(address_as_string) - std::strlen(address_as_string), ":%d",
                  ::ntohs(((struct sockaddr_in *)address)->sin_port));
    break;
  case AF_INET6:
    ::inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)address)->sin6_addr), address_as_string, sizeof(address_as_string));
    std::snprintf(address_as_string + std::strlen(address_as_string), sizeof(address_as_string) - std::strlen(address_as_string), ":%d",
                  ::ntohs(((struct sockaddr_in6 *)address)->sin6_port));
    break;
  }

  SMARTBULB_LOG("connect() " << fd << " to " << address_as_string);

  if(!address_re || std::regex_match(address_as_string, *address_re))
  {
    if(!command_line.empty())
    {
      try
      {
        boost::process::spawn(command_line.data(), std::to_string(fd));
      }
      catch(const boost::process::process_error &error)
      {
        SMARTBULB_LOG("connect() error: \n" << error.what());
      }
    }

    managed_sockets.emplace(fd);
    SMARTBULB_LOG("connect() -> manage " << fd);
  }
  else
    SMARTBULB_LOG("connect() -> not a managed address");

  return result;
}

extern "C" __attribute__((visibility("default"))) ssize_t write(int fd, const void *buf, size_t count)
{
  if(managed_sockets.contains(fd)) [[likely]]
  {
    SMARTBULB_LOG(fd << " writev");
    const iovec iov {.iov_base = const_cast<void *>(buf), .iov_len = count};
    return ::writev(fd, &iov, 1);
  }
  else
  {
    SMARTBULB_LOG(fd << " write");
    return (*default_write)(fd, buf, count);
  }
}

extern "C" __attribute__((visibility("default"))) ssize_t sendmsg(int fd, const struct msghdr *msg, int flags)
{
  if(LIKELY(managed_sockets.contains(fd)))
  {
    SMARTBULB_LOG(fd << " writev");

    assert(!msg->msg_name && !msg->msg_namelen && msg->msg_iov && msg->msg_iovlen && !msg->msg_control && !msg->msg_controllen && !(flags & ~MSG_NOSIGNAL));

    struct sigaction act;

    if(flags == MSG_NOSIGNAL)
    {
      act.sa_handler = SIG_IGN;
      ::sigaction(SIGPIPE, &act, NULL);
    }

    const auto result = ::writev(fd, msg->msg_iov, msg->msg_iovlen);

    if(flags == MSG_NOSIGNAL)
    {
      act.sa_handler = SIG_DFL;
      ::sigaction(SIGPIPE, &act, NULL);
    }

    return result;
  }
  else
  {
    SMARTBULB_LOG(fd << " sendmsg");
    return (*default_sendmsg)(fd, msg, flags);
  }
}

extern "C" __attribute__((visibility("default"))) int close(int fd)
{
  SMARTBULB_LOG(fd << " close");
  managed_sockets.erase(fd);
  return (*default_close)(fd);
}
