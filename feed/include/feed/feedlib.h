#pragma once

#include <cstddef>
#include <cstdint>

#if defined(__cplusplus)
extern "C" {
#endif // defined(__cplusplus)


typedef uint16_t up_instrument_id_t;
typedef int8_t up_field_t;
typedef uint64_t up_timestamp_t;


//
// state

struct up_state;

up_state *up_state_new(up_instrument_id_t instrument);
void up_state_free(up_state *self);
void up_state_update_float(up_state *self, up_field_t field, float value);
void up_state_update_uint(up_state *self, up_field_t field, uint32_t value);


//
// encoder

struct up_encoder;

up_encoder *up_encoder_new();
void up_encoder_free(up_encoder *self);
size_t up_encoder_encode(up_encoder *self, up_timestamp_t timestamp, const up_state *states, size_t nb_states, void *buffer,
                         size_t buffer_size);


//
// decoder

struct up_decoder;

up_decoder *up_decoder_new();
void up_decoder_free(up_decoder *self);
size_t up_decoder_decode(up_decoder *self, up_timestamp_t timestamp, const up_state *states, size_t nb_states,
                         const void *buffer, size_t buffer_size);


//
// server 

struct up_server;

up_server *up_server_new(const char *snapshot_host, const char *snapshot_service, const char *updates_host, const char *updates_service);
void up_server_free(up_server *self);
size_t up_server_poll(up_server *self);
void up_server_push_update(up_server *self, const up_state *states, size_t nb_states);

#if defined(__cplusplus)
} // extern "C"
#endif // defined(__cplusplus)

