#pragma once

#include <stdbool.h>
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
    NTRIP_STATE_CONNECTED,
    NTRIP_STATE_STREAMING,
    NTRIP_STATE_RECONNECT_WAIT,
    NTRIP_STATE_ERROR
} ntrip_state_t;

typedef struct {
    ntrip_state_t state;
    byte_ring_buffer_t rtcm_buffer;
    uint8_t rtcm_storage[512];
    uint32_t connect_attempts;
    uint32_t reconnect_delay_cycles;
    uint32_t retry_cycles_left;
    uint32_t bytes_received;
    uint32_t bytes_dropped;
    int32_t last_error;
} ntrip_client_t;

void ntrip_client_init(ntrip_client_t* client);
void ntrip_client_request_connect(ntrip_client_t* client);
void ntrip_client_service_step(ntrip_client_t* client);
void ntrip_client_report_error(ntrip_client_t* client, int32_t error_code);
size_t ntrip_client_feed_rtcm(ntrip_client_t* client, const uint8_t* data, size_t length);
size_t ntrip_client_pop_rtcm(ntrip_client_t* client, uint8_t* out_data, size_t max_length);
size_t ntrip_client_peek_rtcm(const ntrip_client_t* client, uint8_t* out_data, size_t max_length);
bool ntrip_client_is_streaming(const ntrip_client_t* client);

#ifdef __cplusplus
}
#endif
