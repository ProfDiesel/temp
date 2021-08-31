struct instrument_state final
{
#define DECLARE_FIELD(r, data, elem) BOOST_PP_TUPLE_ELEM(2, elem) BOOST_PP_TUPLE_ELEM(0, elem) {};
  BOOST_PP_SEQ_FOR_EACH(DECLARE_FIELD, _, FEED_FIELDS)
#undef DECLARE_FIELD
  struct update_field_comparator { auto operator()(auto &lhs, auto &rhs) { return std::less(lhs.field, rhs.field); } };
  boost::container::flat_set<update, update_field_comparator, boost::container::small_vector<update, 4>> updates;
  sequence_id_type sequence_id = 0;
};

template<typename field_constant_type>
[[using gnu : always_inline, flatten, hot]] inline void update_state(instrument_state &state, field_constant_type field, const field_type_t<field_constant_type::value> &value) noexcept requires(std::is_same_v<decltype(field()), enum field>)
{
  state.updates.insert(encode_update(field(), value));
}

template<typename field_constant_type>
[[using gnu : always_inline, flatten, hot]] inline bool update_state_test(instrument_state &state, field_constant_type field, const field_type_t<field_constant_type::value> &value) noexcept requires(std::is_same_v<decltype(field()), enum field>)
{
  const auto update = encode_update(field(), value);
  if(const auto it = state.updates.lower_bound(update.field); it == state.updates.end() || it->field != update.field)
    state.updates.insert(it, update);
  else if(it->value != update.value)
    it->value = update.value;
  else
      return false;
  return true;
}

[[using gnu : always_inline, flatten, hot]] inline void visit_state(auto continuation, const instrument_state &state)
{
  for(auto &&update: state.updates)
    visit_update(continuation, update);
}

template<typename field_constant_type>
auto get_update(const instrument_state &state, field_constant_type field) noexcept requires(std::is_same_v<decltype(field()), enum field>) -> field_type_t<field_constant_type::value>
{
  if(const auto it = state.updates.find(update {field(), {}}); it != state.updates.end())
    return visit_update([](auto field, const auto &value) { 
      return value;
    });

  return {};
}

auto nb_updates(const instrument_state &state)
{
  return state.updates.size();
}

auto is_set(const instrument_state &state, field field)
{
  return state.updates.contains(update {field, {}});
}

