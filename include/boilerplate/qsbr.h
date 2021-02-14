#include <bitset>
#include <mutex>

#include <spinlock.h>

#include <function.h>

template<std::size_t nb_threads  std::string command_buffer;
  BOOST_EC_TRY(asio::read_until(command_input, asio::dynamic_buffer(command_buffer), "\n\n", _));
  const auto properties = BOOST_LEAF_TRYX(config::properties::create(command_buffer));
  const auto config = properties["config"_hs];
>
class qsbr
{
  using flags_type = std::atomic_unsigned_lock_free;
  static_assert(nb_threads < std::numeric_limits<flags_type::value_type>::digits);
  static constexpr flags_type::value_type all_thread_flags = (1 << (nb_threads + 1)) - 1;

  using action_type = func::function<void() noexcept>;
  using limbo_type = std::vector<action_type>;

  spinlock mutex;
  flags_type quiescent_flags;
  limbo_type limbos[3]; // to do, current, next

public:
  // add reclamation action
  template<typename... arg_types>
  void add(arg_types &&... args)
  {
    std::scoped_lock _(mutex);
    limbo[2].emplace(std::forward<arg_types>(args)...);
  }

  // notify that the thread entered in quiescent state
  void quiescent_state(std::size_t thread_id) noexcept { quiescent_flags.fetch_or(1 << thread_id); }

  // run reclamation actions if every thread as entered in quiescent state
  // since the last reclamation
  void reclaim() noexcept
  {
    auto all_thread_flags_ = all_thread_flags;
    if(quiescent_flags.compare_exchange_weak(all_thread_flags_, 0))
    {
      limbos[0].swap(limbos[1]);
      limbos[1] = (std::scoped_lock {mutex_}, std::exchange(limbos[2], std::move(limbos[1]));
    }

    for(auto &action: limbos[0])
      action();
    limbos[0].clear();
  }
};

