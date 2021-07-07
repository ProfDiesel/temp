// récupérer: wiring:trigger_
/*
  const auto trigger_ = [](auto &&continuation, const auto &feed_timestamp, auto &&update, auto *automaton) noexcept [[using gnu : always_inline, flatten, hot]] {
    return (automaton->trigger)(continuation, feed_timestamp, std::move(update));
  };
*/
/*
   trigger_dispatcher::with_trigger : captures par valeur, probablement faire la meme chose pour wiring::with_trigger_dispatcher
                       doctest
 */



namespace invariant
{
// triggers
// jamais dans le buffer de situation de trigger (ou automaton en cooldown)

// cooldown doit etre résistant aux souscriptions dynamiques
} // namespace invariant

namespace invariant
{
using histo_type = std::deque<std::tuple<clock::time_point, feed::update>>;

template<typename trigger_type>
auto check_opportunity([[maybe_unused]] const trigger_type &trigger, const histo_type &histo)
{
  return false;
}

template<>
auto check_opportunity(const trigger_dispatcher<std::tuple<std::tuple<std::tuple<feed::b0_c, feed::o0_c>, move_trigger<feed::price_t>>>> &trigger, const histo_type &histo)
{
  const auto handle_update = [&](auto field, const auto &value) { return false; };
  return fn::any(histo % fn::transform L(feed::visit_update(hanndle_update, _.second)));
}

struct checker 
{
  std::unordered_map<instrument_id_type, histo_type> histo_by_instrument;

  auto on_update()(instrument_id_type instrument, const clock::time_point &timestamp, const feed::update &update) noexcept;
  {
    histo_by_instrument[instrument].emplace_back(timestamp, update);
  }

  auto operator()(const automata &automata) const noexcept
  {
    const auto trigger_opportunity = L(check_opportunity(_.trigger, histo_by_instrument[_.instrument_id]));
    const auto in_cooldown = L(automata.instrument_maybe_disabled(_.instrument_id) == nullptr);

    // every automaton either
    //  - has no trigger opportunity in recent history
    // or (exclusive)
    //  - is in cooldown
    return fn::all(automata.all() % fn::map L(!trigger_opportunity(_) ^ in_cooldown(_));
  }
};
} // namespace invariant
