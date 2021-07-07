#pragma once

#include "../config/config_reader.hpp"

#include <boilerplate/leaf.hpp>

#include <asio/buffer.hpp>

#include <boost/range/iterator_range.hpp>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <type_traits>

namespace detail
{
struct owning_aligned_buffer final
{
  static constexpr auto alignement_value = 1'024;
  static constexpr auto alignment = std::align_val_t {alignement_value};

  struct deleter
  {
    void operator()(std::byte *ptr) noexcept { ::operator delete(ptr, alignment); }
  };
  /*const*/ std::unique_ptr<std::byte[], deleter> data = nullptr;
  /*const*/ std::size_t size = 0;

  owning_aligned_buffer() = default;

  template<typename iterator_type>
  owning_aligned_buffer(iterator_type first, std::size_t size) noexcept: data(static_cast<std::byte *>(::operator new[](size, alignment))), size(size)
  {
    std::copy_n(first, size, data.get());
  }

  template<typename iterator_type>
  owning_aligned_buffer(iterator_type first, iterator_type last) noexcept: owning_aligned_buffer(first, std::size_t(std::distance(first, last)))
  {
  }

  operator asio::const_buffer() const noexcept { return asio::const_buffer(data.get(), size); }
};

struct null_buffer final
{
  operator asio::const_buffer() const noexcept { return asio::const_buffer(nullptr, 0); }
};

} // namespace detail

template<bool send_datagram>
struct payload final
{
  /*const*/ detail::owning_aligned_buffer stream_payload = {};
  [[no_unique_address]] /*const*/ std::conditional_t<send_datagram, detail::owning_aligned_buffer, detail::null_buffer> datagram_payload = {};
};

template<bool send_datagram>
boost::leaf::result<payload<send_datagram>> decode_payload(const config::walker &walker) noexcept
{
  const auto base64 = [&](/*TODO: std::string_view s*/ const std::string &s) noexcept -> boost::leaf::result<detail::owning_aligned_buffer> {
    constexpr std::array lookup_table = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, //
                                         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, //
                                         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63, //
                                         52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1, //
                                         -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, //
                                         15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1, //
                                         -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, //
                                         41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1};

    const auto last = std::find_if(s.crbegin(), s.crbegin() + 2, [](auto c) { return c != '='; }).base();
    std::vector<std::byte> buffer {};
    buffer.reserve(s.size() * 4 / 3 - static_cast<std::size_t>(std ::distance(last, s.end())));
    auto out = std::back_inserter(buffer);
    auto acc = 0, bit_offset = -8;
    for(auto c: boost::make_iterator_range(s.cbegin(), last))
    {
      const auto x = lookup_table[static_cast<unsigned char>(c)];
      if(x == -1) [[unlikely]]
        return BOOST_LEAF_NEW_ERROR(c);
      acc = (acc << 6) + x;
      bit_offset += 6;
      if(bit_offset >= 0)
      {
        *out++ = std::byte(acc >> bit_offset);
        bit_offset -= 8;
      }
    }
    return detail::owning_aligned_buffer {buffer.begin(), buffer.end()};
  };

  using namespace config::literals;
  if constexpr(send_datagram)
    return payload<send_datagram> {.stream_payload = BOOST_LEAF_TRYX(base64(*walker["message"_hs])),
                                   .datagram_payload = BOOST_LEAF_TRYX(base64(*walker["datagram"_hs]))};
  else
    return payload<send_datagram> {.stream_payload = BOOST_LEAF_TRYX(base64(*walker["message"_hs]))};
}

#if defined(DOCTEST_LIBRARY_INCLUDED)
// GCOVR_EXCL_START

#  include <fn.hpp>

#  include <string_view>

#  define L(expr) ([&](auto &&_) { return expr; })

TEST_SUITE("payload")
{
  using namespace config::literals;
  using namespace std::string_view_literals;

  namespace fn = rangeless::fn;
  using fn::operators::operator%;

  TEST_CASE("decode")
  {
    boost::leaf::try_handle_all(
      [&]() -> boost::leaf::result<void> {
        const auto properties = BOOST_LEAF_TRYX(config::properties::create("\
payloads.message <- 'c3RyZWFtX3BheWxvYWQ=';\
payloads.datagram <- 'ZGF0YWdyYW1fcGF5bG9hZA==';"sv));

        const auto payload = BOOST_LEAF_TRYX(decode_payload<true>(properties["payloads"_hs]));
        const asio::const_buffer stream_payload = payload.stream_payload, datagram_payload = payload.datagram_payload;

        CHECK((reinterpret_cast<std::uintptr_t>(stream_payload.data()) % 1'024) == 0);
        CHECK((reinterpret_cast<std::uintptr_t>(datagram_payload.data()) % 1'024) == 0);

        const auto check = [](const auto &buffer, const auto &expected) {
          const auto expected_ = expected % fn::transform L(std::byte(_)) % fn::to_vector();
          return std::equal(reinterpret_cast<const std::byte *>(buffer.data()), reinterpret_cast<const std::byte *>(buffer.data()) + buffer.size(),
                            expected_.begin(), expected_.end());
        };
        CHECK(check(stream_payload, "stream_payload"sv));
        CHECK(check(datagram_payload, "datagram_payload"sv));

        return {};
      },
      [&]([[maybe_unused]] const boost::leaf::error_info &unmatched) { CHECK(false); });
  }
}

// GCOVR_EXCL_STOP
#endif // defined(DOCTEST_LIBRARY_INCLUDED)
