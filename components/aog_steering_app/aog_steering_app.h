#pragma once

/* aog_steering_app.h — AOG Steering Application Component.
 *
 * Parses incoming AOG steering frames (PGN 252, 253) from the AOG RX
 * byte ring buffer. Produces:
 *   - SteeringInput snapshot (for steering_control consumption)
 *   - Steering Status output frames (via AOG TX buffer)
 *
 * Data flow (canonical):
 *   transport_udp RX Buffer
 *     -> aog_steering_app (parse PGN 252/253, produce status)
 *     -> SteeringInput Snapshot
 *       -> steering_control
 *         -> SteeringCommand Snapshot
 *           -> actuator_drv8263h
 *
 * This component does NOT:
 *   - call transport functions (no UDP, UART, TCP)
 *   - call HAL functions
 *   - use heap allocation
 *   - reference GNSS/NTRIP/RTCM modules
 *   - contain any steering control logic
 *   - directly control actuators
 *
 * IMPORTANT: runtime_component_t MUST be the first field for safe casting. */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "runtime_component.h"
#include "aog_frame.h"
#include "aog_pgn.h"
#include "byte_ring_buffer.h"
#include "snapshot_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Status output interval ---- */

#define AOG_STEER_STATUS_INTERVAL_MS  50  /* 20 Hz */

/* ---- AOG Steering App Instance ---- */

typedef struct {
    runtime_component_t component;    /* MUST be first */

    /* RX/TX buffers (NOT owned, set by caller) */
    byte_ring_buffer_t* aog_rx_source;   /* incoming AOG frames */
    byte_ring_buffer_t* aog_tx_dest;     /* outgoing AOG frames */

    /* Parsed steering input (latest value) */
    aog_steer_input_t steer_input;
    bool steer_input_valid;

    /* SteeringInput snapshot for steering_control consumption */
    snapshot_buffer_t steer_input_snapshot;
    aog_steer_input_t steer_input_storage;

    /* Steering output state */
    aog_steer_status_t steer_status;
    bool hello_response_pending;

    /* Frame source address for outgoing AOG frames (default: AOG_SRC_STEER) */
    uint8_t src_address;

    /* AOG frame parser */
    aog_parser_t aog_parser;

    /* Status send timing */
    uint64_t last_status_send_us;

    /* Stats */
    uint32_t cycle_count;
} aog_steering_app_t;

/* ---- API ---- */

/* Initialize steering application. Memsets to zero, inits parser,
 * inits snapshot buffer, sets service_step callback. */
void aog_steering_app_init(aog_steering_app_t* app);

/* Set AOG RX source buffer (NOT owned). Called during wiring. */
void aog_steering_app_set_aog_rx_source(aog_steering_app_t* app,
                                         byte_ring_buffer_t* source);

/* Set AOG TX destination buffer (NOT owned). Called during wiring. */
void aog_steering_app_set_aog_tx_dest(aog_steering_app_t* app,
                                       byte_ring_buffer_t* dest);

/* Service step: read AOG frames, parse steering input, produce status output.
 * Called by the runtime service loop. */
void aog_steering_app_service_step(runtime_component_t* comp,
                                    uint64_t timestamp_us);

/* Get the runtime_component pointer (for registration). */
runtime_component_t* aog_steering_app_get_component(aog_steering_app_t* app);

/* Set the source address used in outgoing AOG frames (default: AOG_SRC_STEER).
 * Call before starting service if a non-default source is needed. */
void aog_steering_app_set_src_address(aog_steering_app_t* app, uint8_t src);

/* Get the SteeringInput snapshot buffer (for wiring to steering_control).
 * Returns read-only pointer to the snapshot (NOT the data itself). */
const snapshot_buffer_t* aog_steering_app_get_steer_input_snapshot(
    const aog_steering_app_t* app);

#ifdef __cplusplus
}
#endif
