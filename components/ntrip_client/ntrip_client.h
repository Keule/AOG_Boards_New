#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "runtime_component.h"
#include "byte_ring_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- NTRIP Client State Machine ---- */

typedef enum {
    NTRIP_STATE_IDLE = 0,
    NTRIP_STATE_CONNECTING,
    NTRIP_STATE_AUTHENTICATING,
    NTRIP_STATE_CONNECTED,
    NTRIP_STATE_ERROR,
    NTRIP_STATE_RECONNECT
} ntrip_state_t;

/* RTCM output buffer size */
#define NTRIP_RTCM_BUFFER_SIZE  512

/* Skeleton state transition timeouts (microseconds).
 * Simulate TCP handshake and auth delays in skeleton mode.
 * In a real implementation these would be driven by actual TCP events. */
#define NTRIP_SKELETON_CONNECTING_TIMEOUT_US    1000000u  /* 1 second */
#define NTRIP_SKELETON_AUTHENTICATING_TIMEOUT_US 1000000u  /* 1 second */

/* ---- NTRIP Client Instance ----
 *
 * Independent NTRIP skeleton with own state machine and RTCM output buffer.
 * Consumes TCP data from an external source buffer (set by caller).
 * Provides RTCM data via pop/read API.
 *
 * This component does NOT own the TCP transport or the RTCM router.
 *
 * Skeleton mode:
 *   After calling ntrip_client_start(), the state machine auto-progresses:
 *   IDLE -> CONNECTING -> AUTHENTICATING -> CONNECTED
 *   using simulated timeouts. In CONNECTED state, RTCM data from the
 *   TCP source buffer is forwarded to the RTCM output buffer.
 *
 * IMPORTANT: runtime_component_t MUST be the first field for safe casting. */
typedef struct {
    runtime_component_t component;    /* MUST be first */

    /* State machine */
    ntrip_state_t state;
    bool         started;              /* true after ntrip_client_start() called */
    uint32_t     reconnect_count;
    uint64_t     last_state_change_us;

    /* TCP data source (set by caller, NOT owned) */
    byte_ring_buffer_t* tcp_source;

    /* RTCM output buffer (owned) */
    uint8_t             rtcm_storage[NTRIP_RTCM_BUFFER_SIZE];
    byte_ring_buffer_t  rtcm_buffer;
} ntrip_client_t;

/* ---- API ---- */

/* Initialize NTRIP client (resets state machine and RTCM buffer).
 * tcp_source may be NULL and set later via ntrip_client_set_tcp_source(). */
void ntrip_client_init(ntrip_client_t* client);

/* Start the NTRIP connection sequence.
 * Transitions from IDLE to CONNECTING. After that, service_step() will
 * auto-progress through the skeleton state machine using simulated timeouts.
 * Returns true if start was triggered, false if not in IDLE state. */
bool ntrip_client_start(ntrip_client_t* client);

/* Set the TCP data source buffer (e.g., transport_tcp.rx_buffer).
 * The source is NOT owned by the client. */
void ntrip_client_set_tcp_source(ntrip_client_t* client, byte_ring_buffer_t* source);

/* Transition to a new state.
 * Returns true if transition was valid, false if invalid. */
bool ntrip_client_transition(ntrip_client_t* client, ntrip_state_t new_state, uint64_t timestamp_us);

/* Get current state */
ntrip_state_t ntrip_client_get_state(const ntrip_client_t* client);

/* Check if the client has been started (ntrip_client_start() was called). */
bool ntrip_client_is_started(const ntrip_client_t* client);

/* Get state name as string (for debugging/logging) */
const char* ntrip_client_state_name(ntrip_state_t state);

/* Get reconnect counter */
uint32_t ntrip_client_get_reconnect_count(const ntrip_client_t* client);

/* ---- RTCM Data API ---- */

/* Service step: manage state machine progression, read from TCP source.
 * When state == CONNECTED, incoming data is stored in the RTCM output buffer.
 * Skeleton mode: auto-progresses CONNECTING -> AUTHENTICATING -> CONNECTED
 * with simulated timeouts after ntrip_client_start() has been called.
 * Called by the runtime service loop. */
void ntrip_client_service_step(runtime_component_t* comp, uint64_t timestamp_us);

/* Pop RTCM bytes from the output buffer.
 * Returns number of bytes read into buf. */
size_t ntrip_client_pop_rtcm(ntrip_client_t* client, uint8_t* buf, size_t max_len);

/* Get number of RTCM bytes available in the output buffer. */
size_t ntrip_client_rtcm_available(const ntrip_client_t* client);

#ifdef __cplusplus
}
#endif
