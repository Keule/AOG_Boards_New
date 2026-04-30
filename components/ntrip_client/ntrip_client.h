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

/* ---- NTRIP Error Codes ---- */

typedef enum {
    NTRIP_OK = 0,
    NTRIP_ERR_NOT_CONFIGURED,
    NTRIP_ERR_NOT_CONNECTED,
    NTRIP_ERR_REQUEST_TOO_LARGE,
    NTRIP_ERR_AUTH_FAILED,
    NTRIP_ERR_NOT_FOUND,
    NTRIP_ERR_FORBIDDEN,
    NTRIP_ERR_TIMEOUT,
    NTRIP_ERR_DISCONNECTED,
    NTRIP_ERR_INVALID_PARAM,
    NTRIP_ERR_INTERNAL,
    NTRIP_ERR_TRANSPORT_WRITE,
} ntrip_err_t;

/* ---- NTRIP Client State Machine ---- */

typedef enum {
    NTRIP_STATE_IDLE = 0,
    NTRIP_STATE_CONNECTING,       /* TCP connect in progress */
    NTRIP_STATE_BUILD_REQUEST,    /* Building HTTP GET request */
    NTRIP_STATE_SEND_REQUEST,     /* Sending request (partial-write-safe) */
    NTRIP_STATE_WAIT_RESPONSE,    /* Waiting for HTTP response */
    NTRIP_STATE_CONNECTED,        /* Connected, forwarding RTCM data */
    NTRIP_STATE_ERROR,            /* Error occurred (transient, auto → RETRY_WAIT) */
    NTRIP_STATE_RETRY_WAIT,       /* Backoff before retry */
} ntrip_state_t;

/* ---- NTRIP Client Config ---- */

#define NTRIP_MAX_HOST_LEN         128
#define NTRIP_MAX_MOUNTPOINT_LEN   128
#define NTRIP_MAX_CRED_LEN         128
#define NTRIP_MAX_USER_AGENT_LEN   128

typedef struct {
    char     host[NTRIP_MAX_HOST_LEN];
    uint16_t port;
    char     mountpoint[NTRIP_MAX_MOUNTPOINT_LEN];
    char     username[NTRIP_MAX_CRED_LEN];
    char     password[NTRIP_MAX_CRED_LEN];
    char     user_agent[NTRIP_MAX_USER_AGENT_LEN];
    uint32_t timeout_ms;            /* HTTP response timeout */
    uint32_t reconnect_backoff_ms;  /* Backoff after error before retry */
} ntrip_client_config_t;

#define NTRIP_CLIENT_CONFIG_DEFAULT() { \
    .host = "",                         \
    .port = 2101,                       \
    .mountpoint = "",                   \
    .username = "",                     \
    .password = "",                     \
    .user_agent = "NTRIP AOG-Multiboard/1.0", \
    .timeout_ms = 10000,                \
    .reconnect_backoff_ms = 5000        \
}

/* ---- NTRIP Request/Response Buffer Sizes ---- */

#define NTRIP_REQUEST_BUFFER_SIZE  256
#define NTRIP_RESPONSE_BUFFER_SIZE 256

/* ---- NTRIP Client Instance ----
 *
 * Independent NTRIP client with own state machine, request builder, and
 * RTCM output buffer.  Consumes TCP data via a transport_tcp_t reference.
 * Provides RTCM data via pop/read API.
 *
 * This component does NOT own the TCP transport.  It contains no
 * socket or LWIP details — all TCP I/O goes through the transport_tcp API.
 *
 * IMPORTANT: runtime_component_t MUST be the first field for safe casting. */
typedef struct {
    runtime_component_t component;    /* MUST be first */

    /* State machine */
    ntrip_state_t state;
    bool         started;
    bool         connect_attempted;  /* true once transport_tcp_connect() has been called */
    uint32_t     reconnect_count;
    uint64_t     last_state_change_us;

    /* Configuration (copied from caller) */
    ntrip_client_config_t config;
    bool                  config_valid;

    /* Transport reference (set by caller, NOT owned) */
    transport_tcp_t* transport;

    /* Request buffer for partial-write-safe TX */
    uint8_t request_buf[NTRIP_REQUEST_BUFFER_SIZE];
    size_t request_len;
    size_t request_sent_offset;

    /* Response buffer for HTTP status parsing */
    uint8_t response_buf[NTRIP_RESPONSE_BUFFER_SIZE];
    size_t response_received;

    /* Error tracking */
    ntrip_err_t last_error;
    int         http_status_code;

    /* RTCM output buffer (owned) */
    uint8_t             rtcm_storage[512];
    byte_ring_buffer_t  rtcm_buffer;
} ntrip_client_t;

/* ---- API ---- */

/* Initialize NTRIP client (resets state machine and RTCM buffer).
 * Config and transport may be set later via configure/set_transport. */
void ntrip_client_init(ntrip_client_t* client);

/* Set NTRIP connection configuration (host, port, mountpoint, credentials, etc).
 * Config is copied internally.  config_valid is set only if host && mountpoint are non-empty. */
void ntrip_client_configure(ntrip_client_t* client, const ntrip_client_config_t* config);

/* Set the TCP transport reference (e.g., &s_ntrip_tcp).
 * The transport is NOT owned by the client.  Must be set before start(). */
void ntrip_client_set_transport(ntrip_client_t* client, transport_tcp_t* transport);

/* Start the NTRIP connection sequence.
 * Returns true if start was triggered, false if not in IDLE state,
 * config is invalid, or transport is NULL. */
bool ntrip_client_start(ntrip_client_t* client);

/* Transition to a new state.
 * Returns true if transition was valid, false if invalid. */
bool ntrip_client_transition(ntrip_client_t* client, ntrip_state_t new_state, uint64_t timestamp_us);

/* Get current state */
ntrip_state_t ntrip_client_get_state(const ntrip_client_t* client);

/* Check if the client has been started. */
bool ntrip_client_is_started(const ntrip_client_t* client);

/* Get state name as string (for debugging/logging) */
const char* ntrip_client_state_name(ntrip_state_t state);

/* Get reconnect counter */
uint32_t ntrip_client_get_reconnect_count(const ntrip_client_t* client);

/* Get last error code */
ntrip_err_t ntrip_client_get_last_error(const ntrip_client_t* client);

/* Get last HTTP status code (0 if not yet parsed) */
int ntrip_client_get_http_status(const ntrip_client_t* client);

/* ---- Service step ---- */

/* Service step: manage state machine progression, partial-write TX,
 * HTTP response parsing, RTCM forwarding.
 * Called by the runtime service loop. */
void ntrip_client_service_step(runtime_component_t* comp, uint64_t timestamp_us);

/* ---- RTCM Data API ---- */

/* Pop RTCM bytes from the output buffer.
 * Returns number of bytes read into buf. */
size_t ntrip_client_pop_rtcm(ntrip_client_t* client, uint8_t* buf, size_t max_len);

/* Get number of RTCM bytes available in the output buffer. */
size_t ntrip_client_rtcm_available(const ntrip_client_t* client);

/* ---- Legacy compatibility ---- */

/* DEPRECATED: Use ntrip_client_set_transport() instead.
 * Kept for incremental migration only. */
void ntrip_client_set_tcp_source(ntrip_client_t* client, byte_ring_buffer_t* source);

#ifdef __cplusplus
}
#endif
