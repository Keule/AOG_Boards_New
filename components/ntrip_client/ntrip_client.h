#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NTRIP_STATE_IDLE = 0,
    NTRIP_STATE_CONNECTING,
    NTRIP_STATE_AUTHENTICATING,
    NTRIP_STATE_CONNECTED,
    NTRIP_STATE_ERROR_RECONNECT
} ntrip_state_t;

typedef struct {
    ntrip_state_t state;
    uint32_t reconnect_attempts;
    int32_t last_error;
} ntrip_client_t;

void ntrip_client_init(ntrip_client_t* client);
void ntrip_client_request_connect(ntrip_client_t* client);
void ntrip_client_on_connected(ntrip_client_t* client);
void ntrip_client_on_authenticated(ntrip_client_t* client);
void ntrip_client_on_error(ntrip_client_t* client, int32_t error_code);
void ntrip_client_step(ntrip_client_t* client);

#ifdef __cplusplus
}
#endif
