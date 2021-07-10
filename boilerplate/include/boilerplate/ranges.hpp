#pragma once

#include <boilerplate/likely.hpp>

#include <range/v3/range/operations.hpp>
#include <range/v3/view/view.hpp>

#include <optional>

namespace boilerplate
{
	namespace r = ranges::v3;
	namespace v = ranges::v3::view;

	struct optional_front_fn
	{
    template<typename range_type>
		constexpr auto operator()(range_type &&range) const { return LIKELY(!r::empty(range)) ? std::optional(r::front(range)) : std::nullopt; }
	};

	RANGES_INLINE_VARIABLE(v::view_closure<optional_front_fn>, optional_front)
}
