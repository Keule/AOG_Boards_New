#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "runtime_component.h"
#include "byte_ring_buffer.h"
#include "transport_tcp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===================================================================
 * NAV-NTRIP-001: Produktiver NTRIP-Client auf generischem TCP-Transport
 *
 * Data flow:
 *   ntrip_client → transport_tcp connect → HTTP GET + Basic Auth
 *                → ICY 200 OK / HTTP 200 → STREAMING
 *                → transport_tcp RX → ntrip_client.rtcm_buffer
 *
 * This component does NOT own the TCP transport or the RTCM router.
 * It does NOT contain RTCM routing logic, UART output, or AOG PGN handling.
 * =================================================================== */

/* ---- NTRIP Client States ---- */

typedef enum {
    NTRIP_STATE_IDLE = 0,         /* Not started, waiting for ntrip_client_start() */
    NTRIP_STATE_CONNECTING,       /* TCP connect in progress */
    NTRIP_STATE_SEND_REQUEST,     /* Sending HTTP/NTRIP request to server */
    NTRIP_STATE_WAIT_RESPONSE,    /* Waiting for HTTP response headers */
    NTRIP_STATE_STREAMING,        /* Connected, forwarding RTCM data */
    NTRIP_STATE_ERROR,            /* Error occurred (HTTP error, timeout, disconnect) */
    NTRIP_STATE_RETRY_WAIT        /* Exponential backoff before reconnect */
} ntrip_state_t;

/* ---- Buffer Sizes ---- */

#define NTRIP_RTCM_BUFFER_SIZE       512
#define NTRIP_REQUEST_BUFFER_SIZE    256
#define NTRIP_RESPONSE_BUFFER_SIZE   512

/* ---- Default Configuration Values ---- */

#define NTRIP_DEFAULT_USER_AGENT              "AOG-ESP-Multiboard/1.0"
#define NTRIP_DEFAULT_INITIAL_BACKOFF_MS      1000u
#define NTRIP_DEFAULT_MAX_BACKOFF_MS          60000u
#define NTRIP_DEFAULT_RESPONSE_TIMEOUT_MS     10000u

/* ---- NTRIP Client Configuration ----
 *
 * All string pointers must remain valid for the lifetime of the client.
 * No secrets are hardcoded — configuration is provided at runtime. */

typedef struct {
    const char* mountpoint;              /* NTRIP mountpoint (e.g., "VRTK"), required */
    const char* username;                /* Username for Basic Auth (NULL = no auth) */
    const char* password;                /* Password for Basic Auth (NULL if no auth) */
    const char* user_agent;              /* User-Agent header value (NULL = default) */
    uint32_t reconnect_initial_ms;       /* Initial reconnect backoff in ms */
    uint32_t reconnect_max_ms;           /* Maximum reconnect backoff in ms */
    uint32_t response_timeout_ms;        /* Timeout for connect + response in ms */
} ntrip_client_config_t;

/* ---- NTRIP Client Instance ----
 *
 * Uses transport_tcp for all TCP operations (connect, send, recv).
 * Provides RTCM data via rtcm_buffer (owned ring buffer).
 *
 * IMPORTANT: runtime_component_t MUST be the first field for safe casting. */
typedef struct {
    runtime_component_t component;       /* MUST be first */

    /* State machine */
    ntrip_state_t state;
    bool         started;
    uint32_t     reconnect_count;
    uint64_t     last_state_change_us;

    /* Configuration (pointer to external config, NOT owned) */
    const ntrip_client_config_t* config;

    /* TCP transport (pointer to external transport_tcp_t, NOT owned) */
    transport_tcp_t* tcp;

    /* Request buffer (built HTTP request) */
    uint8_t  request_buf[NTRIP_REQUEST_BUFFER_SIZE];
    size_t   request_len;
    bool     request_sent;

    /* Response buffer (accumulates HTTP response headers) */
    uint8_t  response_buf[NTRIP_RESPONSE_BUFFER_SIZE];
    size_t   response_len;

    /* RTCM output buffer (owned) */
    uint8_t             rtcm_storage[NTRIP_RTCM_BUFFER_SIZE];
    byte_ring_buffer_t  rtcm_buffer;

    /* Backoff state */
    uint32_t current_backoff_ms;

    /* Error tracking */
    int last_error_code;                 /* HTTP status code, or negative for internal errors */

    /* Connect attempt tracking */
    bool connect_attempted;
} ntrip_client_t;

/* ---- API: Lifecycle ---- */

/* Initialize NTRIP client (resets state machine and RTCM buffer).
 * Does not start connection — call ntrip_client_start() afterwards.
 * Call ntrip_client_configure() and ntrip_client_set_transport() before start. */
void ntrip_client_init(ntrip_client_t* client);

/* Set NTRIP configuration. Config pointer must remain valid for client lifetime. */
hal_err_t ntrip_client_configure(ntrip_client_t* client, const ntrip_client_config_t* config);

/* Set TCP transport. Transport pointer must remain valid for client lifetime. */
void ntrip_client_set_transport(ntrip_client_t* client, transport_tcp_t* tcp);

/* Start NTRIP connection sequence.
 * Transitions from IDLE to CONNECTING. Returns true if start triggered. */
bool ntrip_client_start(ntrip_client_t* client);

/* Stop NTRIP client. Transitions any state to IDLE. */
bool ntrip_client_stop(ntrip_client_t* client);

/* ---- API: State Machine ---- */

/* Manually transition to a new state (for testing).
 * Returns true if transition was valid. */
bool ntrip_client_transition(ntrip_client_t* client, ntrip_state_t new_state, uint64_t timestamp_us);

/* Get current state */
ntrip_state_t ntrip_client_get_state(const ntrip_client_t* client);

/* Check if client has been started */
bool ntrip_client_is_started(const ntrip_client_t* client);

/* Get state name as string (for debugging/logging) */
const char* ntrip_client_state_name(ntrip_state_t state);

/* Get reconnect counter */
uint32_t ntrip_client_get_reconnect_count(const ntrip_client_t* client);

/* Get last error code (HTTP status or negative internal error) */
int ntrip_client_get_last_error_code(const ntrip_client_t* client);

/* ---- API: Service Step ---- */

/* Service step: manage state machine, handle TCP events, forward RTCM data.
 * Called by the runtime service loop. Non-blocking. */
void ntrip_client_service_step(runtime_component_t* comp, uint64_t timestamp_us);

/* ---- API: RTCM Data ---- */

/* Pop RTCM bytes from the output buffer.
 * Returns number of bytes read into buf. */
size_t ntrip_client_pop_rtcm(ntrip_client_t* client, uint8_t* buf, size_t max_len);

/* Get number of RTCM bytes available in the output buffer. */
size_t ntrip_client_rtcm_available(const ntrip_client_t* client);

/* ---- API: Configuration Defaults ---- */

/* Fill config struct with default values. Mountpoint/credentials still need to be set. */
void ntrip_client_config_set_defaults(ntrip_client_config_t* config);

#ifdef __cplusplus
}
#endif
