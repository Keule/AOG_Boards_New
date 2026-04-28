#include "rtcm_router.h"
#include <string.h>

void rtcm_router_init(rtcm_router_t* router)
{
    if (router == NULL) {
        return;
    }
    memset(router, 0, sizeof(rtcm_router_t));
    rtcm_passthrough_init(&router->passthrough);
    router->component.service_step = rtcm_router_service_step;
}

int rtcm_router_add_output(rtcm_router_t* router, byte_ring_buffer_t* tx_buffer)
{
    if (router == NULL || tx_buffer == NULL) {
        return -1;
    }
    if (router->output_count >= RTCM_ROUTER_MAX_OUTPUTS) {
        return -2;
    }

    rtcm_output_t* out = &router->outputs[router->output_count];
    out->tx_buffer = tx_buffer;
    out->enabled = true;
    out->bytes_forwarded = 0;
    out->bytes_dropped = 0;

    return (int)router->output_count++;
}

void rtcm_router_set_source(rtcm_router_t* router, byte_ring_buffer_t* source)
{
    if (router == NULL) {
        return;
    }
    router->rtcm_source = source;
}

void rtcm_router_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    rtcm_router_t* router = (rtcm_router_t*)comp;
    if (router == NULL) {
        return;
    }

    if (router->rtcm_source == NULL) {
        return;
    }

    /* Check if RTCM data is available from source */
    size_t available = byte_ring_buffer_available(router->rtcm_source);
    if (available == 0) {
        return;
    }

    /* Read RTCM data from source into local buffer */
    uint8_t tmp[128];
    size_t to_read = available > sizeof(tmp) ? sizeof(tmp) : available;
    size_t pulled = byte_ring_buffer_read(router->rtcm_source, tmp, to_read);
    if (pulled == 0) {
        return;
    }

    /* Record incoming RTCM bytes */
    rtcm_passthrough_record_in(&router->passthrough, pulled, timestamp_us);

    /* Distribute to all registered output buffers */
    for (uint8_t i = 0; i < router->output_count; i++) {
        rtcm_output_t* out = &router->outputs[i];
        if (!out->enabled || out->tx_buffer == NULL) {
            continue;
        }

        size_t written = byte_ring_buffer_write(out->tx_buffer, tmp, pulled);
        if (written > 0) {
            out->bytes_forwarded += (uint32_t)written;
            rtcm_passthrough_record_out(&router->passthrough, written);
        }

        if (written < pulled) {
            uint32_t dropped = (uint32_t)(pulled - written);
            out->bytes_dropped += dropped;
            rtcm_passthrough_record_dropped(&router->passthrough, dropped);
        }
    }
}

const rtcm_stats_t* rtcm_router_get_stats(const rtcm_router_t* router)
{
    if (router == NULL) {
        return NULL;
    }
    return rtcm_passthrough_get_stats(&router->passthrough);
}
