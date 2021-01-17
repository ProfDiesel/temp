#pragma once

#include <asio.hpp>
#include <coroutine>

#include <type_traits>
#include <variant>

namespace boilerplate
{
namespace detail
{
// make it a lambda in initiate_co_spawn
template<typename executor_type, typename function_type, typename handler_type>
asio::awaitable<void, executor_type> co_spawn_entry_point(executor_type ex, function_type f, handler_type handler)
{
  auto spawn_work = make_co_spawn_work_guard(ex);
  auto handler_work = make_co_spawn_work_guard(asio::get_associated_executor(handler, ex));

  co_await asio::post(spawn_work.get_executor(), asio::use_awaitable_t<executor_type> {});

  auto t = co_await f();
  asio::dispatch(handler_work.get_executor(), [handler = std::move(handler), t = std::move(t)]() mutable { handler(std::move(t)); });
}

template<typename executor_type_>
struct initiate_co_spawn
{
  typedef executor_type_ executor_type;

  executor_type executor;

  auto get_executor() const noexcept { return executor; }

  template<typename handler_type, typename function_type>
  void operator()(handler_type &&handler, function_type &&f) const
  {
    auto awaitable = co_spawn_entry_point(executor, std::forward<function_type>(f), std::forward<handler_type>(handler));
    asio::detail::awaitable_handler<executor_type, void>(std::move(awaitable), executor).launch();
  }
};

} // namespace detail

template<typename executor_type, typename value_type, typename awaitable_executor_type, typename completion_token_type>
inline auto co_spawn(const executor_type &executor, asio::awaitable<value_type, awaitable_executor_type> awaitable, completion_token_type &&token)
{
  using initiation_type = initiate_co_spawn<awaitable_executor_type>;
  using completion_signature = void(value_type);
  asio::detail::awaitable_as_function<value_type, awaitable_executor_type> arg0(std::move(awaitable));
  const auto f = asio::async_initiate<completion_token_type, void(value_type), initiation_type, decltype(arg0)>;
  return f(initiation_type {.executor = awaitable_executor_type(executor)}, token, std::move(arg0));
}

} // namespace boilerplate

