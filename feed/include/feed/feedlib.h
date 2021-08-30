#pragma once

#include <stdbool.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C"
{
#endif // defined(__cplusplus)

  typedef uint16_t up_instrument_id_t;
  typedef uint32_t up_sequence_id_t;
  typedef uint64_t up_timestamp_t;

  // TODO Make X-Macros from feed_fields.hpp
  enum up_field
  {
    field_b0 = 10,
    field_bq0 = 20,
    field_o0 = 30,
    field_oq0 = 40
  };


  //
  // state

  struct up_state;

  struct up_state *up_state_new(up_instrument_id_t instrument);
  void up_state_free(struct up_state *self);
  up_sequence_id_t up_state_get_sequence_id(const struct up_state *self);
  void up_state_set_sequence_id(struct up_state *self, up_sequence_id_t sequence_id);
  bool up_state_is_set(const struct up_state *self, enum up_field field);
  float up_state_get_value_float(const struct up_state *self, enum up_field field);
  uint32_t up_state_get_value_uint(const struct up_state *self, enum up_field field);
  void up_state_update_float(struct up_state *self, enum up_field field, float value);
  void up_state_update_uint(struct up_state *self, enum up_field field, uint32_t value);


  //
  // encoder

  struct up_encoder;

  struct up_encoder *up_encoder_new();
  void up_encoder_free(struct up_encoder *self);
  size_t up_encoder_encode(struct up_encoder *self, up_timestamp_t timestamp, const struct up_state *const states[], size_t nb_states,
                           void *buffer, size_t buffer_size);


  //
  // decoder

  struct up_decoder;

  typedef void (*up_on_message_t)(const struct up_state*, void*);

  struct up_decoder *up_decoder_new(up_on_message_t on_message, void *user_data);
  void up_decoder_free(struct up_decoder *self);
  size_t up_decoder_decode(struct up_decoder *self, const void *buffer, size_t buffer_size);


  //
  // future

  struct up_future;

  struct up_future *up_future_new();
  void up_future_free(struct up_future *self);
  void up_future_set_ok(struct up_future *self);
  void up_future_set_message(struct up_future *self, const char *message);
  bool up_future_is_set(const struct up_future *self);
  bool up_future_is_ok(const struct up_future *self);
  const char *up_future_get_message(const struct up_future *self);



  //
  // server

  struct up_server;

  struct up_server *up_server_new(const char *snapshot_host, const char *snapshot_service, const char *updates_host, const char *updates_service,
                                  struct up_future *future);
  void up_server_free(struct up_server *self);
  size_t up_server_poll(struct up_server *self, struct up_future *future);
  struct up_future *up_server_push_update(struct up_server *self, const struct up_state *const states[], size_t nb_states);
  struct up_future *up_server_replay(struct up_server *self, const void *buffer, size_t buffer_size);
  void up_server_get_state(struct up_server *self, up_instrument_id_t instrument, struct up_state *state);

#if defined(__cplusplus)
} // extern "C"
#endif // defined(__cplusplus)
