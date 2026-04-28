#include "rtcm_router.h"

#include <string.h>

#include "protocol_rtcm.h"
#include "transport_uart.h"
#include "hal_uart.h"

#define RTCM_ROUTER_BUFFER_SIZE 512U

static uint8_t s_input_buffer[RTCM_ROUTER_BUFFER_SIZE];
static size_t s_input_length = 0;
static uint64_t s_last_input_time = 0;
static rtcm_counters_t s_counters;

static void rtcm_router_fast_process(runtime_component_t* component, const fast_cycle_context_t* ctx)
{
    uint8_t out_buffer[RTCM_ROUTER_BUFFER_SIZE];
    size_t out_size = 0;
    size_t written = 0;

    (void)component;

    if (s_input_length == 0) {
        return;
    }

    out_size = rtcm_passthrough_process(
        &s_counters,
        s_input_buffer,
        s_input_length,
        out_buffer,
        sizeof(out_buffer),
        (ctx != 0) ? ctx->timestamp_us : s_last_input_time
    );

    if (out_size > 0) {
        transport_uart_write_nonblocking(HAL_UART_PORT_GNSS_PRIMARY, out_buffer, out_size, &written);
        transport_uart_write_nonblocking(HAL_UART_PORT_GNSS_SECONDARY, out_buffer, out_size, &written);
    }

    s_input_length = 0;
}

static runtime_component_t s_component = {
    .name = "rtcm_router",
    .user_data = 0,
    .fast_input = 0,
    .fast_process = rtcm_router_fast_process,
    .fast_output = 0,
};

int rtcm_router_init(void)
{
    s_input_length = 0;
    s_last_input_time = 0;
    rtcm_passthrough_init(&s_counters);
    return 0;
}

runtime_component_t* rtcm_router_component(void)
{
    return &s_component;
}

void rtcm_router_push_from_ntrip(const uint8_t* data, size_t length, uint64_t now_us)
{
    if (data == 0 || length == 0) {
        return;
    }

    if (length > RTCM_ROUTER_BUFFER_SIZE) {
        length = RTCM_ROUTER_BUFFER_SIZE;
    }

    memcpy(s_input_buffer, data, length);
    s_input_length = length;
    s_last_input_time = now_us;
}
