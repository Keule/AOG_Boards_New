#pragma once

#include <stddef.h>
#include <stdint.h>

#include "byte_ring_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NTRIP_STATE_IDLE = 0,
    NTRIP_STATE_CONNECTING,
    NTRIP_STATE_AUTHENTICATING,
    NTRIP_STATE_STREAMING
} ntrip_state_t;

typedef struct {
    ntrip_state_t state;
    byte_ring_buffer_t rtcm_buffer;
    uint8_t rtcm_storage[512];
} ntrip_client_t;

void ntrip_client_init(ntrip_client_t* client);
void ntrip_client_request_connect(ntrip_client_t* client);
void ntrip_client_service_step(ntrip_client_t* client);
size_t ntrip_client_feed_rtcm(ntrip_client_t* client, const uint8_t* data, size_t length);
size_t ntrip_client_pop_rtcm(ntrip_client_t* client, uint8_t* out_data, size_t max_length);
size_t ntrip_client_peek_rtcm(const ntrip_client_t* client, uint8_t* out_data, size_t max_length);

#ifdef __cplusplus
}
#endif
