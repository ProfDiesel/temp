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

  typedef enum up_field
  {
    b0 = 1,
    bq0 = 2,
    o0 = 3,
    oq0 = 4
  } up_field_t;


  //
  // state

  struct up_state;
  typedef struct up_state up_state_t;

  up_state_t *up_state_new(up_instrument_id_t instrument);
  void up_state_free(up_state_t *self);
  up_sequence_id_t up_state_get_sequence_id(up_state_t *self);
  void up_state_set_sequence_id(up_state_t *self, up_sequence_id_t sequence_id);
  uint64_t up_state_get_bitset(const up_state_t *self);
  float up_state_get_float(const up_state_t *self, up_field_t field);
  uint32_t up_state_get_uint(const up_state_t *self, up_field_t field);
  void up_state_update_float(up_state_t *self, up_field_t field, float value);
  void up_state_update_uint(up_state_t *self, up_field_t field, uint32_t value);


  //
  // encoder

  struct up_encoder;
  typedef struct up_encoder up_encoder_t;

  up_encoder_t *up_encoder_new();
  void up_encoder_free(up_encoder_t *self);
  size_t up_encoder_encode(up_encoder_t *self, up_timestamp_t timestamp, const up_state_t *const states[], size_t nb_states,
                                 void *buffer, size_t buffer_size);


  //
  // decoder

  struct up_decoder;
  typedef struct up_decoder up_decoder_t;

  typedef void (*up_on_message_t)(up_instrument_id_t, void *user_data);
  typedef void (*up_on_update_float_t)(up_field_t, float, void *user_data);
  typedef void (*up_on_update_uint_t)(up_field_t, size_t, void *user_data);

  up_decoder_t *up_decoder_new(up_on_update_float_t on_update_float, up_on_update_uint_t on_update_uint, void *user_data);
  void up_decoder_free(up_decoder_t *self);
  size_t up_decoder_decode(up_decoder_t *self, const void *buffer, size_t buffer_size);


  //
  // future

  struct up_future;
  typedef struct up_future up_future_t;

  up_future_t *up_future_new();
  void up_future_free(up_future_t *self);
  void up_future_set_ok(up_future_t *self);
  void up_future_set_message(up_future_t *self, const char *message);
  bool up_future_is_set(const up_future_t *self);
  bool up_future_is_ok(const up_future_t *self);
  const char *up_future_get_message(const up_future_t *self);



  //
  // server

  struct up_server;
  typedef struct up_server up_server_t;

  up_server_t *up_server_new(const char *snapshot_host, const char *snapshot_service, const char *updates_host, const char *updates_service,
                             up_future_t *future);
  void up_server_free(up_server_t *self);
  size_t up_server_poll(up_server_t *self, up_future_t *future);
  up_future_t *up_server_push_update(up_server_t *self, const up_state_t *const states[], size_t nb_states);
  up_future_t *up_server_replay(up_server_t *self, const void *buffer, size_t buffer_size);
  void up_server_get_state(up_server_t *self, up_instrument_id_t instrument, up_state_t *state);

#if defined(__cplusplus)
} // extern "C"
#endif // defined(__cplusplus)
