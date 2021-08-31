class decimal
{
	using underlying_type = std::uint32_t;

	constexpr static auto exponent_digits = 10;
	auto sign: 1;
	auto exponent: exponent_digits; // in complement to 2
	auto significant: std::numeric_limits<underlying_type>::digits() - exponent_digits - 1;

  constexpr auto operator<=>(decimal other) const noexcept
	{
	}

	template<typename value_type>
	constexpr value_type max_for_exponent(exponent_type exponent)
	{
	}
};

struct step
{
	decimal from;
	decimal tick;

	constexpr decimal max()
	{
	}

	constexpr auto prefered_exponent()
	{
	}
};

template<step steps...>
struct tick_rule
{
	decimal encode(auto value)
	{
		for(auto &&step: steps)
		{
		}
	}
};

