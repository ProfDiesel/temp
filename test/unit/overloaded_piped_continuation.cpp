#include <boilerplate.hpp>
#include <piped_continuation.hpp>
#include <variant>

using update = std::variant<int, std::nullptr_t>;

template<typename continuation_type>
auto decode_update(continuation_type continuation, update update) noexcept
{
  if(update.index() == 0)
   return std::forward<continuation_type>(continuation)(update.index(), std::get<0>(update));
  else
   return std::forward<continuation_type>(continuation)(update.index(), std::get<1>(update));
}

auto decode_update_ = BOOST_HOF_LIFT(decode_update);

inline auto g(update update) noexcept
{
    return (decode_update_ |= overloaded {
        [](auto index, int x) noexcept { return x; },
        [](auto index, auto x) noexcept { return 42; }
    })(update);
}

int main(int argc, char *argv[])
{
    return g(argc);
}

