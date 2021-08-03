#include "common/config_reader.hpp"

#include <boost/leaf/common.hpp>
#include <boost/leaf/handle_errors.hpp>

#include <iostream>

auto main() -> int
{
  using namespace logger::literals;

  return leaf::try_handle_all(
    [&]() noexcept -> leaf::result<void> {
      const auto _ = BOOST_LEAF_TRYX(config::properties::create(std::cin));
    },
    [&](const leaf::error_info &unmatched) noexcept { std::abort(); });
}

