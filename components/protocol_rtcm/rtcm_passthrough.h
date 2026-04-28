#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- RTCM Passthrough Stats ---- */

typedef struct {
    uint32_t bytes_in;
    uint32_t bytes_out;
    uint32_t bytes_dropped;
    uint64_t last_activity_us;
} rtcm_stats_t;

/* RTCM passthrough handler */
typedef struct {
    rtcm_stats_t stats;
} rtcm_passthrough_t;

/* Initialize passthrough handler (reset all counters) */
void rtcm_passthrough_init(rtcm_passthrough_t* rtcm);

/* Record incoming bytes.
 * timestamp_us: current timestamp in microseconds (provided by caller). */
void rtcm_passthrough_record_in(rtcm_passthrough_t* rtcm, size_t length, uint64_t timestamp_us);

/* Record outgoing bytes */
void rtcm_passthrough_record_out(rtcm_passthrough_t* rtcm, size_t length);

/* Record dropped bytes */
void rtcm_passthrough_record_dropped(rtcm_passthrough_t* rtcm, size_t length);

/* Update last activity timestamp */
void rtcm_passthrough_update_activity(rtcm_passthrough_t* rtcm, uint64_t timestamp_us);

/* Get current stats (read-only pointer, valid until next record call) */
const rtcm_stats_t* rtcm_passthrough_get_stats(const rtcm_passthrough_t* rtcm);

#ifdef __cplusplus
}
#endif
