#include <functional>
struct function_traits<>
{
  using arg_types = std::tuple<>;
};

template<typename value_type>
struct generator
{
  static std::tupple<value_type, std::span<std::byte>> generate(std::span<std::byte> input)
  {
    value_type result;
    auto it = std::copy(span, result);
    return {result, {it, span.end()}};
};

template<typename function_type>
auto call_mutate(function_type &&function, std::span<std::byte> input)
{
  return std::apply([&](auto... &&args) {}, std::declval<typename function_traits<function_type>::args_types>());
}

