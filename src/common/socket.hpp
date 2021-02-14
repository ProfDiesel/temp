#pragma once

#include <boilerplate/boilerplate.hpp>
#include <boilerplate/chrono.hpp>
#include <boilerplate/leaf.hpp>
#include <boilerplate/outcome.hpp>
#include <boilerplate/pointers.hpp>

#include <asio/buffer.hpp>
#include <asio/ip/multicast.hpp>
#include <asio/ip/udp.hpp>

#include <gsl/util>

#include <iostream>
#if defined(LINUX)
#  include <linux/net_tstamp.h>
#  include <sched.h>
#endif // defined(LINUX)
#include <string_view>

#if defined(USE_TCPDIRECT)
#  include <etherfabric/vi.h>
#  include <zf/zf.h>
#endif // defined(USE_TCPDIRECT)

#if defined(USE_TCPDIRECT)

class deleters
{
  static void operator()(zfur *ptr) { ::zfur_free(ptr); };
  static void operator()(zfut *ptr) { ::zfut_free(ptr); };
  static void operator()(zf_attr *ptr) { ::zf_attr_free(ptr); };
  static void operator()(zf_stack *ptr) { ::zf_stack_free(ptr); };
};

struct static_stack
{
  static_stack() noexcept
  {
    TRY(::zf_init());

    zf_attr *attr = nullptr;
    TRY(::zf_attr_alloc(&attr));
    TRY(::zf_attr_set_str(attr, "interface", interface));
    TRY(::zf_attr_set_str(attr, "ctpio_mode", "ct"));
    TRY(::zf_attr_set_str(attr, "rx_timestamping", 1));

    zf_stack *stack = nullptr;
    TRY(::zf_stack_alloc(attr, &stack));

    ef_driver_handle dh;
    TRY(::ef_driver_open(&dh));
    ef_pd pd;
    TRY(::ef_pd_alloc_by_name(&pd, dh, interface, EF_PD_DEFAULT));
    ef_vi vi;
    TRY(::ef_vi_alloc_from_pd(&vi, dh, &pd, dh, -1, 0, -1, nullptr, -1, EF_VI_FLAGS_DEFAULT));
    ef_pio pio;
    TRY(::ef_pio_alloc(&pio, dh, &pd, -1, dh));
    TRY(::ef_pio_link_vi(&pio, dh, &vi, dh));
  }
};

#endif // defined(USE_TCPDIRECT)

using namespace std::chrono_literals;

//
//
// UDP multicast reader
#if defined(USE_TCPDIRECT)
class multicast_udp_reader final
#else
class multicast_udp_reader final : public asio::ip::udp::socket
#endif
{
  // no IP (v4 or v6) packet can be bigger than 64kb
  static constexpr auto buffer_size = 65'536;

  static constexpr auto nb_messages = 32;

public:
#if defined(USE_TCPDIRECT)
  static out::result<multicast_udp_reader> create(asio::io_context &service, boilerplate::not_null_observer_ptr<zf_stack> stack, std::string_view address,
                                                  std::string_view port) noexcept
  {
    const auto addrinfo = OUTCOME_EC_TRYX(asio::ip::udp::resolver(service).resolve(address, port, _)).begin()->endpoint().data();

    zfur zock_ {0};
    OUTCOME_TRY(::zfur_alloc(&zock_, stack, attr));
    zock_ptr zock {zock_};

    OUTCOME_TRY(::zfur_addr_bind(zock, addrinfo->ai_addr, addrinfo->ai_addrlen, nullptr, 0, 0));

    return multicast_udp_reader {stack, std::move(zock)};
  }
#else
#  if defined(LINUX)
  static out::result<multicast_udp_reader> create(asio::io_context &service, std::string_view address, std::string_view port,
                                                  const std::chrono::nanoseconds &spin_duration) noexcept
#  else  // defined(LINUX)
  static out::result<multicast_udp_reader> create(asio::io_context &service, std::string_view address, std::string_view port) noexcept
#  endif // defined(LINUX)
  {
    const auto endpoint = OUTCOME_EC_TRYX(asio::ip::udp::resolver(service).resolve(address, port, _)).begin()->endpoint();
    asio::ip::udp::socket socket {service};
    OUTCOME_EC_TRYV(socket.open(endpoint.protocol(), _));
    OUTCOME_EC_TRYV(socket.set_option(asio::ip::udp::socket::reuse_address(true), _));

#  if defined(LINUX)
    using busy_poll = asio::detail::socket_option::integer<SOL_SOCKET, SO_BUSY_POLL>;
    using incoming_cpu = asio::detail::socket_option::integer<SOL_SOCKET, SO_INCOMING_CPU>;
    // NOLINTNEXTLINE(hicpp-signed-bitwise)
    using timestamping
      = asio::detail::socket_option::boolean<SOL_SOCKET, SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE | SOF_TIMESTAMPING_SOFTWARE>;

    const auto cpu = ::sched_getcpu();


    OUTCOME_EC_TRYV(socket.set_option(incoming_cpu(cpu), _));
    // TODO : make configuration dependant
    if(false)
      OUTCOME_EC_TRYV(socket.set_option(timestamping(true), _));

    auto as_timeval = to_timeval(spin_duration);
    OUTCOME_TRY(out::result<void>(std::error_code(::setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &as_timeval, sizeof(as_timeval)),
                                                  std::generic_category()))); // recvmmsg timeout parameter is buggy
#  endif // defined(LINUX)

    socket.bind(endpoint);
    OUTCOME_EC_TRYV(socket.set_option(asio::ip::multicast::join_group(endpoint.address()), _));

#  if defined(LINUX)
    return multicast_udp_reader(std::move(socket), spin_duration);
#  else  // defined(LINUX)
    return multicast_udp_reader(std::move(socket));
#  endif // defined(LINUX)
  }
#endif   // defined(USE_TCPDIRECT)

  [[using gnu : always_inline, flatten, hot]] auto operator()(auto &continuation) noexcept
    -> out::result<bool>
  {
#if defined(USE_TCPDIRECT)
    ::zf_reactor_perform(stack);

    struct
    {
      zfur_msg msg {.iovcnt = 1};
      iovec iov[1];
    } msg;

    ::zfur_zc_recv(zock.get(), &msg.msg, 0);
    auto _ = gsl::finally([&]() { ::zfur_zc_recv_done(zock.get(), &msg.msg); });
    if(!msg.msg.iovcnt)
      return out::success();

    timespec timestamp;
    unsigned int timestamp_flags;
    OUTCOME_TRY(::zfur_pkt_get_timestamp(zock.get(), &msg.msg, &timestamp, 0, &timestamp_flags));

    return continuation(timestamp, asio::const_buffer(reinterpret_cast<const std::byte *>(msg.msg.iov[0].iov_base, msg.msg.iov[0].iov_len)));

#elif defined(LINUX)
    auto spin_duration = spin_duration_;
    const auto nb_messages_read
      = ::recvmmsg(native_handle(), msgvec_.data(), nb_messages, MSG_WAITFORONE, &spin_duration); // recvmmsg timeout parameter is buggy
    if(nb_messages_read <= 0)
      return std::error_code(errno, std::generic_category());

    const auto get_timestamp = [](msghdr *msg) {
      for(auto *cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      {
        if(cmsg->cmsg_level != SOL_SOCKET)
          continue;

        switch(cmsg->cmsg_type)
        {
        case SO_TIMESTAMPNS:
        case SO_TIMESTAMPING: return to_time_point<network_clock>(*reinterpret_cast<const std::timespec *>(CMSG_DATA(cmsg)));
        }
      }
      return network_clock::time_point {};
    };

    const auto timestamp = get_timestamp(&msgvec_[0].msg_hdr);
    auto result = continuation(timestamp, asio::const_buffer(reinterpret_cast<const std::byte *>(buffers_[0].data()), msgvec_[0].msg_len));

    for(auto i = 1; i < nb_messages_read; ++i)
      result |= continuation(timestamp, asio::const_buffer(reinterpret_cast<const std::byte *>(buffers_[i].data()), msgvec_[i].msg_len));

    return out::success();
#else  // defined(LINUX)
    asio::const_buffer buffer(buffer_.data(), buffer_size);
    const auto msg_length = receive(buffer_);
    return continuation({}, buffer_);
#endif // defined(LINUX)
  }

private:
#if defined(USE_TCPDIRECT)
  using zock_ptr = std::unique_ptr<zfur, deleters>;

  const boilerplate::not_null_observer_ptr<zf_stack> stack;
  zock_ptr zock;
#elif defined(LINUX)
  const std::timespec spin_duration_;

  std::array<std::array<char, buffer_size>, nb_messages> buffers_ {};

  template<std::size_t... indices>
  std::array<iovec, nb_messages> make_iovecs(decltype(buffers_) &buffers, [[maybe_unused]] std::index_sequence<indices...> _)
  {
    return {iovec {.iov_base = buffers[indices].data(), .iov_len = buffer_size}...};
  }
  std::array<iovec, nb_messages> iovecs_ = make_iovecs(buffers_, std::make_index_sequence<nb_messages>());

  template<std::size_t... indices>
  std::array<mmsghdr, nb_messages> make_msgvec(decltype(iovecs_) &iovecs, [[maybe_unused]] std::index_sequence<indices...> _)
  {
    return {mmsghdr {.msg_hdr = {.msg_iov = &iovecs[indices], .msg_iovlen = 1}, .msg_len = 0}...};
  }
  std::array<mmsghdr, nb_messages> msgvec_ = make_msgvec(iovecs_, std::make_index_sequence<nb_messages>());

  multicast_udp_reader(asio::ip::udp::socket &&socket, const std::chrono::nanoseconds &spin_duration):
    asio::ip::udp::socket(std::move(socket)), spin_duration_(to_timespec(spin_duration))
  {
  }
#else  // defined(LINUX)
  std::array<char, buffer_size> buffer_ {};

  explicit multicast_udp_reader(asio::ip::udp::socket && socket): asio::ip::udp::socket(std::move(socket)) {}
#endif // defined(USE_TCPDIRECT)
};

//
//
// UDP writer
#if defined(USE_TCPDIRECT)
class udp_writer final
#else
class udp_writer final : public asio::ip::udp::socket
#endif
{
public:
#if defined(USE_TCPDIRECT)
  static out::result<udp_writer> create(asio::io_context &service, boilerplate::not_null_observer_ptr<zf_stack> stack, std::string_view address,
                                        std::string_view port) noexcept
  {
    const auto addrinfo = OUTCOME_EC_TRYX(asio::ip::udp::resolver(service).resolve(address, port, _))->endpoint().data();

    zfut zock_ {0};
    TRY(::zfut_alloc(&udp_sock, stack, INADDR_ANY, 0, addrinfo->ai_addr, addrinfo->ai_addrlen, 0, attr));
    zock_ptr zock {zock_};

    return udp_writer {stack, std::move(zock)};
  }
#else // defined(USE_TCPDIRECT)
  static out::result<udp_writer> create(asio::io_context &service, std::string_view address, std::string_view port) noexcept
  {
    const auto endpoint = OUTCOME_EC_TRYX(asio::ip::udp::resolver(service).resolve(address, port, _)).begin()->endpoint();
    asio::ip::udp::socket socket {service};
    OUTCOME_EC_TRYV(socket.connect(endpoint, _));
    return udp_writer {std::move(socket)};
  }
#endif

#if defined(USE_TCPDIRECT)
  [[using gnu : always_inline, flatten, hot]] out::result<network_clock::time_point> send(const asio::const_buffer &buffer) noexcept
  {
    ::zfut_send_single(*zock_, buffer.data, buffer.size);
    return ::zfut_timestamp();
  }
  [[using gnu : noinline, cold]] out::result<network_clock::time_point> send_blank(const asio::const_buffer &buffer) noexcept
  {
    ::zfut_send_single_warm(*zock_, buffer.data, buffer.size);
    return {};
  }
#else  // defined(USE_TCPDIRECT)
  [[using gnu : always_inline, flatten, hot]] out::result<network_clock::time_point> send(const asio::const_buffer &buffer) noexcept
  {
    OUTCOME_EC_TRYV(asio::ip::udp::socket::send(buffer, 0, _));
    return network_clock::now();
  }
  [[using gnu : noinline, cold]] out::result<network_clock::time_point> send_blank([[maybe_unused]] const asio::const_buffer &) noexcept { return network_clock::now(); }
#endif // defined(USE_TCPDIRECT)

private:
#if defined(USE_TCPDIRECT)
  using zock_ptr = std::unique_ptr<zfut, deleters>;

  const boilerplate::not_null_observer_ptr<zf_stack> stack_;
  zock_ptr zock_;

  udp_writer(boilerplate::not_null_observer_ptr<zf_stack> stack, zock_ptr &&zock) noexcept: stack_(stack), zock_(std::move(zock)) {}
#endif // defined(USE_TCPDIRECT)
};
