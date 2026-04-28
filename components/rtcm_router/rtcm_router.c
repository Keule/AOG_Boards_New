#include "rtcm_router.h"

#include <string.h>

static void rtcm_router_component_step(runtime_component_t* component)
{
    rtcm_router_t* router = 0;
    if (component == 0) {
        return;
    }

    router = (rtcm_router_t*)component->user_data;
    rtcm_router_step(router);
}

void rtcm_router_init(rtcm_router_t* router, byte_ring_buffer_t* input_buffer)
{
    if (router == 0) {
        return;
    }

    memset(&router->stats, 0, sizeof(router->stats));
    router->output_count = 0;
    router->input_buffer = input_buffer;
    router->component.name = "rtcm_router";
    router->component.user_data = router;
    router->component.step = rtcm_router_component_step;
}

int rtcm_router_register_output(rtcm_router_t* router, byte_ring_buffer_t* output_buffer)
{
    if (router == 0 || output_buffer == 0 || router->output_count >= 4) {
        return -1;
    }

    router->output_buffers[router->output_count++] = output_buffer;
    return 0;
}

size_t rtcm_router_push_input(rtcm_router_t* router, const uint8_t* data, size_t length)
{
    size_t pushed = 0;

    if (router == 0 || router->input_buffer == 0 || data == 0) {
        return 0;
    }

    pushed = byte_ring_buffer_push(router->input_buffer, data, length);
    router->stats.bytes_in += (uint32_t)pushed;
    router->stats.dropped_bytes += (uint32_t)(length - pushed);
    if (pushed > 0) {
        router->stats.last_activity_cycle = router->stats.cycles;
    }
    return pushed;
}

void rtcm_router_step(rtcm_router_t* router)
{
    uint8_t chunk[64];
    size_t i = 0;
    size_t n = 0;

    if (router == 0 || router->input_buffer == 0) {
        return;
    }

    router->stats.cycles++;

    n = byte_ring_buffer_pop(router->input_buffer, chunk, sizeof(chunk));
    if (n == 0) {
        return;
    }

    for (i = 0; i < router->output_count; ++i) {
        size_t pushed = byte_ring_buffer_push(router->output_buffers[i], chunk, n);
        router->stats.bytes_out += (uint32_t)pushed;
        router->stats.dropped_bytes += (uint32_t)(n - pushed);
    }
    router->stats.last_activity_cycle = router->stats.cycles;
}

void rtcm_router_get_stats(const rtcm_router_t* router, rtcm_router_stats_t* out_stats)
{
    if (router == 0 || out_stats == 0) {
        return;
    }

    *out_stats = router->stats;
}

runtime_component_t* rtcm_router_component(rtcm_router_t* router)
{
    return (router != 0) ? &router->component : 0;
}
