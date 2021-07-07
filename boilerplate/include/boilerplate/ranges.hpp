#pragma once

#include <boilerplate/likely.hpp>

#include <range/v3/empty.hpp>
#include <range/v3/front.hpp>
#include <range/v3/view/view.hpp>

#include <range/v3/utility/static_const.hpp>

#include <optional>

namespace boilerplate
{
	namespace r = ranges::v3;
	namespace v = r::view;

	struct optional_front_fn
	{
		template<typename range_type>
		constexpr auto operator()(range_type &&range) const { return LIKELY(!r::empty(range)) ? std::optional(r::front(range)) : std::nullopt; }
	};

#if defined(CRIPPLED)
	namespace
	{
		constexpr auto&& optional_front = r::static_const<v::view<optional_front_fn>>::value;
	}
#else // defined(CRIPPLED)
	RANGES_INLINE_VARIABLE(v::view<optional_front_fn>, optional_front)
#endif // defined(CRIPPLED)
}
