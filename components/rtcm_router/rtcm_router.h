#pragma once

#include <stddef.h>
#include <stdint.h>

#include "byte_ring_buffer.h"
#include "runtime_component.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t bytes_in;
    uint32_t bytes_out;
    uint32_t dropped_bytes;
    uint32_t cycles;
    uint32_t last_activity_cycle;
} rtcm_router_stats_t;

typedef struct {
    byte_ring_buffer_t* output_buffers[4];
    size_t output_count;
    byte_ring_buffer_t* input_buffer;
    rtcm_router_stats_t stats;
    runtime_component_t component;
} rtcm_router_t;

void rtcm_router_init(rtcm_router_t* router, byte_ring_buffer_t* input_buffer);
int rtcm_router_register_output(rtcm_router_t* router, byte_ring_buffer_t* output_buffer);
size_t rtcm_router_push_input(rtcm_router_t* router, const uint8_t* data, size_t length);
void rtcm_router_step(rtcm_router_t* router);
void rtcm_router_get_stats(const rtcm_router_t* router, rtcm_router_stats_t* out_stats);
runtime_component_t* rtcm_router_component(rtcm_router_t* router);

#ifdef __cplusplus
}
#endif
