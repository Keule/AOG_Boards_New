#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t bytes_in;
    uint64_t bytes_out;
    uint64_t dropped;
    uint64_t last_activity;
} rtcm_counters_t;

void rtcm_passthrough_init(rtcm_counters_t* counters);
size_t rtcm_passthrough_process(
    rtcm_counters_t* counters,
    const uint8_t* in_data,
    size_t in_size,
    uint8_t* out_data,
    size_t out_capacity,
    uint64_t now_us
);

#ifdef __cplusplus
}
#endif
