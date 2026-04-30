#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "runtime_component.h"
#include "rtcm_passthrough.h"
#include "byte_ring_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- RTCM Router Config ---- */

#define RTCM_ROUTER_MAX_OUTPUTS  2

/* ---- RTCM Router Output Target ---- */

typedef struct {
    byte_ring_buffer_t* tx_buffer;   /* output ring buffer (NOT owned by router) */
    bool                enabled;
    uint32_t            bytes_forwarded;
    uint32_t            bytes_dropped;
} rtcm_output_t;

/* ---- RTCM Router Instance ----
 *
 * Distributes RTCM data from a generic source buffer to registered output buffers.
 * Does NOT own the source buffer (set by caller via pointer).
 * Does NOT own the output buffers (set by caller via pointer).
 * Does NOT call HAL, transport, or UART functions.
 * Does NOT depend on ntrip_client or any specific data source type.
 *
 * IMPORTANT: runtime_component_t MUST be the first field for safe casting. */
typedef struct {
    runtime_component_t component;    /* MUST be first */

    rtcm_passthrough_t passthrough;
    byte_ring_buffer_t* rtcm_source;  /* generic RTCM input (NOT owned) */
    rtcm_output_t      outputs[RTCM_ROUTER_MAX_OUTPUTS];
    uint8_t            output_count;
    uint32_t           output_overflow_count;  /* number of times any output buffer was full */
} rtcm_router_t;

/* ---- API ---- */

/* Initialize RTCM router. */
void rtcm_router_init(rtcm_router_t* router);

/* Register an output ring buffer target.
 * tx_buffer: pointer to an externally owned byte_ring_buffer_t.
 * The router writes RTCM data into this buffer; the owner drains it.
 * Returns output index or negative error. */
int rtcm_router_add_output(rtcm_router_t* router, byte_ring_buffer_t* tx_buffer);

/* Set the generic RTCM data source.
 * The source is NOT owned by the router.
 * Typically points to ntrip_client.rtcm_buffer or any other RTCM byte source. */
void rtcm_router_set_source(rtcm_router_t* router, byte_ring_buffer_t* source);

/* Service step: pull RTCM data from source, distribute to outputs.
 * Called by the runtime service loop. */
void rtcm_router_service_step(runtime_component_t* comp, uint64_t timestamp_us);

/* Get RTCM statistics (bytes_in, bytes_out, bytes_dropped, last_activity_us). */
const rtcm_stats_t* rtcm_router_get_stats(const rtcm_router_t* router);

/* Get number of output overflow events (times any output buffer was full). */
uint32_t rtcm_router_get_output_overflow_count(const rtcm_router_t* router);

#ifdef __cplusplus
}
#endif
