#if defined(TEST)
// GCOVR_EXCL_START
#  include <boost/ut.hpp>

namespace ut = boost::ut;

ut::suite up_suite = [] {
  using namespace ut;

  "up_server"_test = [] {
    auto *s = up_server_new("127.0.0.1", "4400", "127.0.0.1", "4401");
    up_server_poll(s);

    feed::instrument_state instrument_state;
    feed::update_state_poly(instrument_state, feed::field::b0, 10.0);
    feed::update_state_poly(instrument_state, feed::field::bq0, 1);
    instrument_state.sequence_id = 5;

    up_update_state state {42, instrument_state};
    up_server_push_update(s, &state, 1);

    for(int i = 0; i < 100; ++i)
      up_server_poll(s);

    up_server_free(s);
  };
};

// GCOVR_EXCL_STOP
#endif // defined(TEST)


