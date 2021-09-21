#pragma once

#include <boilerplate/boilerplate.hpp>
#include <boilerplate/chrono.hpp>
#include <boilerplate/leaf.hpp>
#include <boilerplate/likely.hpp>
#include <boilerplate/pointers.hpp>

#include <asio/buffer.hpp>
#include <asio/ip/multicast.hpp>
#include <asio/ip/udp.hpp>

#include <gsl/util>

#include <range/v3/view/take.hpp>

#include <concepts>
#include <iostream>
#if defined(LINUX)
#  include <linux/net_tstamp.h>
#  include <sched.h>
#endif // defined(LINUX)
#include <string_view>

#if defined(USE_TCPDIRECT)
#  include <etherfabric/pd.h>
#  include <etherfabric/pio.h>
#  include <etherfabric/vi.h>
#  include <zf/zf.h>
#endif // defined(USE_TCPDIRECT)

#if defined(USE_LIBVMA)
#  include <vma/vma_extra.h>
#endif // defined(USE_LIBVMA)

#if defined(USE_TCPDIRECT)

struct deleters
{
  void operator()(::zfur *ptr) { ::zfur_free(ptr); };
  void operator()(::zfut *ptr) { ::zfut_free(ptr); };
  void operator()(::zf_attr *ptr) { ::zf_attr_free(ptr); };
  void operator()(::zf_stack *ptr) { ::zf_stack_free(ptr); };
};

struct static_stack
{
  std::unique_ptr<::zf_attr, deleters> attr;
  std::unique_ptr<::zf_stack, deleters> stack;

  ::ef_driver_handle dh;
  ::ef_pd pd;
  ::ef_vi vi;
  ::ef_pio pio;

  // set -x ZF_ATTR "interface=enp4s0f0;log_level=3:ctpio_mode=ct;rx_timestamping=1"
  static_stack() noexcept
  {
    const auto r = init();
    assert(r);
  }

  static const auto &instance() noexcept
  {
    static static_stack instance;
    return instance;
  }

private:
  [[nodiscard]] boost::leaf::result<void> init()
  {
    BOOST_LEAF_RC_TRYV(::zf_init());

    ::zf_attr *attr = nullptr;
    BOOST_LEAF_RC_TRYV(::zf_attr_alloc(&attr));
    this->attr.reset(attr);

    ::zf_stack *stack = nullptr;
    BOOST_LEAF_RC_TRYV(::zf_stack_alloc(attr, &stack));
    this->stack.reset(stack);

    char *interface = nullptr;
    BOOST_LEAF_RC_TRYV(::zf_attr_get_str(attr, "interface", &interface));

    BOOST_LEAF_RC_TRYV(::ef_driver_open(&dh));
    BOOST_LEAF_RC_TRYV(::ef_pd_alloc_by_name(&pd, dh, interface, EF_PD_DEFAULT));
    BOOST_LEAF_RC_TRYV(::ef_vi_alloc_from_pd(&vi, dh, &pd, dh, -1, 0, -1, nullptr, -1, EF_VI_FLAGS_DEFAULT));
    BOOST_LEAF_RC_TRYV(::ef_pio_alloc(&pio, dh, &pd, -1, dh));
    BOOST_LEAF_RC_TRYV(::ef_pio_link_vi(&pio, dh, &vi, dh));

    return boost::leaf::success();
  }
};

#endif // defined(USE_TCPDIRECT)

#if defined(USE_LIBVMA)
struct vma_api
{
  ::vma_api_t *ptr = nullptr;

  vma_api() noexcept
  {
    const auto r = init();
    assert(r);
  }

  static const auto &instance() noexcept
  {
    static vma_api instance;
    return *instance.ptr;
  }

private:
  [[nodiscard]] boost::leaf::result<void> init()
  {
    ptr = ::vma_get_api();
    if(!ptr)
      return BOOST_LEAF_NEW_ERROR();

    return boost::leaf::success();
  }
};
#endif

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
#if defined(LINUX) && !defined(USE_TCPDIRECT) && !defined(USE_LIBVMA)
  static boost::leaf::result<multicast_udp_reader> create(asio::io_context &service, std::string_view address, std::string_view port,
                                                     const std::chrono::nanoseconds &spin_duration = {}, bool timestamping = false) noexcept
#else  // defined(LINUX)) && !defined(USE_TCPDIRECT) && !defined(USE_LIBVMA)
  static boost::leaf::result<multicast_udp_reader> create(asio::io_context &service, std::string_view address, std::string_view port) noexcept
#endif // defined(LINUX)) && !defined(USE_TCPDIRECT) && !defined(USE_LIBVMA)
  {
    const asio::ip::udp::endpoint endpoint = BOOST_LEAF_EC_TRYX(asio::ip::udp::resolver(service).resolve(address, port, _)).begin()->endpoint();

#if defined(USE_TCPDIRECT)

    ::zfur *zock_ = nullptr;
    BOOST_LEAF_RC_TRYV(::zfur_alloc(&zock_, static_stack::instance().stack.get(), static_stack::instance().attr.get()));
    zock_ptr zock {zock_};

    BOOST_LEAF_RC_TRYV(::zfur_addr_bind(zock.get(), const_cast<sockaddr *>(endpoint.data()), sizeof(*endpoint.data()), nullptr, 0, 0));

    return multicast_udp_reader(std::move(zock));

#else

    asio::ip::udp::socket socket {service};
    BOOST_LEAF_EC_TRYV(socket.open(endpoint.protocol(), _));
    BOOST_LEAF_EC_TRYV(socket.set_option(asio::ip::udp::socket::reuse_address(true), _));

#  if defined(LINUX) && !defined(USE_LIBVMA)
    using busy_poll = asio::detail::socket_option::integer<SOL_SOCKET, SO_BUSY_POLL>;
    using incoming_cpu = asio::detail::socket_option::integer<SOL_SOCKET, SO_INCOMING_CPU>;
    // NOLINTNEXTLINE(hicpp-signed-bitwise)
    using network_timestamping
      = asio::detail::socket_option::boolean<SOL_SOCKET, SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE | SOF_TIMESTAMPING_SOFTWARE>;

    const auto cpu = ::sched_getcpu();

    if(spin_duration.count() > 0)
    {
      BOOST_LEAF_EC_TRYV(socket.set_option(busy_poll(int(std::chrono::duration_cast<std::chrono::microseconds>(spin_duration).count())), _));
      BOOST_LEAF_EC_TRYV(socket.set_option(incoming_cpu(cpu), _));
    }
    if(timestamping)
      BOOST_LEAF_EC_TRYV(socket.set_option(network_timestamping(true), _));

    // recvmmsg timeout parameter is buggy
    const auto as_timeval = to_timeval(spin_duration);
    BOOST_LEAF_EC_TRYV([&]() {
      _ = std::error_code(::setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &as_timeval, sizeof(as_timeval)), std::generic_category());
    }());
#  endif // defined(LINUX) && !defined(USE_LIBVMA)

    socket.bind(endpoint);
    BOOST_LEAF_EC_TRYV(socket.set_option(asio::ip::multicast::join_group(endpoint.address()), _));

#  if defined(USE_LIBVMA)
    int vma_ring_fd = 0;
    const auto rc = vma_api::instance().get_socket_rings_fds(socket.native_handle(), &vma_ring_fd, 1);
    if(rc < 0)
      return BOOST_LEAF_NEW_ERROR();

    return multicast_udp_reader(std::move(socket), vma_ring_fd);
#  elif defined(USE_RECVMMSG)
    return multicast_udp_reader(std::move(socket), spin_duration);
#  else  // defined(USE_RECVMMSG)
    return multicast_udp_reader(std::move(socket));
#  endif // defined(USE_LIBVMA)

#endif   // defined(USE_TCPDIRECT)
  }

  multicast_udp_reader(multicast_udp_reader &&) = default;
  multicast_udp_reader &operator=(multicast_udp_reader &&) = default;

  [[using gnu: always_inline, flatten, hot]] inline auto operator()(std::invocable<const network_clock::time_point&, asio::const_buffer&&> auto continuation) noexcept -> boost::leaf::result<void>
  {
#if defined(USE_TCPDIRECT)
    ::zf_reactor_perform(static_stack::instance().stack.get());

    struct
    {
      zfur_msg msg {.iovcnt = 1};
      iovec iov[1];
    } msg;

    ::zfur_zc_recv(zock_.get(), &msg.msg, 0);
    const auto _ = gsl::finally([&]() { ::zfur_zc_recv_done(zock_.get(), &msg.msg); });
    if(!msg.msg.iovcnt)
      return {};

    timespec timestamp;
    unsigned int timestamp_flags;
    BOOST_LEAF_RC_TRYV(::zfur_pkt_get_timestamp(zock_.get(), &msg.msg, &timestamp, 0, &timestamp_flags));

    continuation(to_time_point<network_clock>(timestamp), asio::const_buffer(msg.msg.iov[0].iov_base, msg.msg.iov[0].iov_len));

#elif defined(USE_LIBVMA)

    const std::size_t nb_completions = vma_api::instance().socketxtreme_poll(vma_ring_fd_, completions_.data(), completions_.size(), 0);
    for(auto &&completion: completions_ | ranges::views::take(nb_completions))
    {
      assert(completion.events & VMA_SOCKETXTREME_PACKET);
      const auto &packet = completion.packet;
      assert(packet.num_bufs == 1);
      continuation(to_time_point<network_clock>(packet.hw_timestamp), asio::const_buffer(packet.buff_lst->payload, packet.total_len));
    }
    for(auto &&completion: completions_ | ranges::views::take(nb_completions))
      vma_api::instance().socketxtreme_free_vma_packets(&completion.packet, 1);

#elif defined(USE_RECVMMSG)
    // recvmmsg timeout parameter is buggy
    auto spin_duration = spin_duration_;
    const std::size_t nb_messages_read = BOOST_LEAF_ERRNO_TRYX(::recvmmsg(native_handle(), msgvec_.data(), nb_messages, MSG_WAITFORONE, &spin_duration), _ > 0);

    const auto get_timestamp = [](msghdr *msg)
    {
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
    for(std::size_t i = 0; i != nb_messages_read; ++i)
      continuation(timestamp, asio::const_buffer(buffers_[i].data(), msgvec_[i].msg_len));

#else  // defined(USE_TCPDIRECT)
    const auto msg_length = receive(asio::buffer(buffer_.data(), buffer_size));
    continuation(network_clock::time_point(), asio::const_buffer(buffer_.data(), msg_length));
#endif // defined(USE_TCPDIRECT)

    return {};
  }

private:
#if defined(USE_TCPDIRECT)
  using zock_ptr = std::unique_ptr<zfur, deleters>;

  zock_ptr zock_;

  explicit multicast_udp_reader(zock_ptr &&zock) noexcept: zock_(std::move(zock)) {}

#elif defined(USE_LIBVMA)
  int vma_ring_fd_;
  std::array<vma_completion_t, 16> completions_;

  multicast_udp_reader(asio::ip::udp::socket && socket, int vma_ring_fd) noexcept: asio::ip::udp::socket(std::move(socket)), vma_ring_fd_(vma_ring_fd) {}

#elif defined(USE_RECVMMSG)
  std::timespec spin_duration_;

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

  multicast_udp_reader(asio::ip::udp::socket &&socket, const std::chrono::nanoseconds &spin_duration) noexcept:
    asio::ip::udp::socket(std::move(socket)), spin_duration_(to_timespec(spin_duration))
  {
  }
#else  // defined(USE_RECVMMSG)
  std::array<char, buffer_size> buffer_ {};

  explicit multicast_udp_reader(asio::ip::udp::socket &&socket) noexcept: asio::ip::udp::socket(std::move(socket)) {}
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
  static boost::leaf::result<udp_writer> create(asio::io_context &service, std::string_view address, std::string_view port) noexcept
  {
#if defined(USE_TCPDIRECT)
    const asio::ip::udp::endpoint addr_local(asio::ip::udp::v4(), 0),
      addr_remote = BOOST_LEAF_EC_TRYX(asio::ip::udp::resolver(service).resolve(address, port, _))->endpoint();

    zfut *zock_ = nullptr;
    BOOST_LEAF_RC_TRYV(::zfut_alloc(&zock_, static_stack::instance().stack.get(), addr_local.data(), sizeof(*addr_local.data()),
                                    const_cast<sockaddr *>(addr_remote.data()), sizeof(*addr_remote.data()), 0, static_stack::instance().attr.get()));
    zock_ptr zock {zock_};

    return udp_writer(std::move(zock));
#else // defined(USE_TCPDIRECT)
    const auto endpoint = BOOST_LEAF_EC_TRYX(asio::ip::udp::resolver(service).resolve(address, port, _)).begin()->endpoint();
    asio::ip::udp::socket socket {service};
    BOOST_LEAF_EC_TRYV(socket.connect(endpoint, _));
    return udp_writer(std::move(socket));
#endif
  }

#if !defined(USE_TCPDIRECT)
  explicit udp_writer(asio::ip::udp::socket &&socket): asio::ip::udp::socket(std::move(socket)) {};
#endif

  udp_writer(udp_writer &&) = default;
  udp_writer &operator=(udp_writer &&) = default;

  [[using gnu: always_inline, flatten, hot]] inline boost::leaf::result<network_clock::time_point> send(const asio::const_buffer &buffer) noexcept
  {
#if defined(USE_TCPDIRECT)
    ::zfut_send_single(zock_.get(), buffer.data(), buffer.size());

    ::zf_pkt_report reports[1];
    int count = 1;
    BOOST_LEAF_RC_TRYV(::zfut_get_tx_timestamps(zock_.get(), reports, &count));
    return LIKELY(count > 0) ? to_time_point<network_clock>(reports[0].timestamp) : network_clock::time_point();
#elif defined(USE_LIBVMA)
    ::send(native_handle(), buffer.data(), buffer.size(), 0);
    return network_clock::now();
#else  // defined(USE_TCPDIRECT)
    BOOST_LEAF_EC_TRYV(asio::ip::udp::socket::send(buffer, 0, _));
    return network_clock::now();
#endif
  }

  [[using gnu: noinline, cold]] boost::leaf::result<network_clock::time_point> send_blank(const asio::const_buffer &buffer) noexcept
  {
#if defined(USE_TCPDIRECT)
    ::zfut_send_single_warm(zock_.get(), buffer.data(), buffer.size());
    return network_clock::time_point();
#elif defined(USE_LIBVMA)
    ::send(native_handle(), buffer.data(), buffer.size(), VMA_SND_FLAGS_DUMMY);
    return network_clock::time_point();
#else  // defined(USE_TCPDIRECT)
    return network_clock::time_point();
#endif
  }

private:
#if defined(USE_TCPDIRECT)
  using zock_ptr = std::unique_ptr<zfut, deleters>;

  zock_ptr zock_;

  udp_writer(zock_ptr &&zock) noexcept: zock_(std::move(zock)) {}
#endif // defined(USE_TCPDIRECT)
};
