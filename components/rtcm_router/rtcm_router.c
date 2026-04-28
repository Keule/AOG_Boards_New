#include "rtcm_router.h"

#include <string.h>

#include "protocol_rtcm.h"
#include "message_queue.h"
#include "ntrip_client.h"

#define RTCM_ROUTER_BUFFER_SIZE 512U
#define RTCM_ROUTER_TX_QUEUE_CAPACITY 32U

static uint8_t s_input_buffer[RTCM_ROUTER_BUFFER_SIZE];
static size_t s_input_length = 0;
static uint64_t s_last_input_time = 0;
static rtcm_counters_t s_counters;
static ntrip_client_t s_ntrip_client;
static message_queue_t s_tx_queue;
static rtcm_router_tx_item_t s_tx_storage[RTCM_ROUTER_TX_QUEUE_CAPACITY];

static void queue_uart_tx(hal_uart_port_t port, const uint8_t* data, size_t length)
{
    rtcm_router_tx_item_t item;

    if (data == 0 || length == 0 || length > sizeof(item.payload)) {
        return;
    }

    item.port = port;
    item.length = length;
    memcpy(item.payload, data, length);
    message_queue_push(&s_tx_queue, &item);
}

static void rtcm_router_fast_process(runtime_component_t* component, const fast_cycle_context_t* ctx)
{
    uint8_t out_buffer[RTCM_ROUTER_BUFFER_SIZE];
    uint8_t ntrip_data[RTCM_ROUTER_BUFFER_SIZE];
    size_t ntrip_length = 0;
    size_t out_size = 0;

    (void)component;

    ntrip_client_step(&s_ntrip_client);

    if (ntrip_client_pop_rtcm(&s_ntrip_client, ntrip_data, sizeof(ntrip_data), &ntrip_length) == 0 && ntrip_length > 0) {
        rtcm_router_push_from_ntrip(ntrip_data, ntrip_length, (ctx != 0) ? ctx->timestamp_us : s_last_input_time);
    }

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
        queue_uart_tx(HAL_UART_PORT_GNSS_PRIMARY, out_buffer, out_size);
        queue_uart_tx(HAL_UART_PORT_GNSS_SECONDARY, out_buffer, out_size);
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
    ntrip_client_init(&s_ntrip_client);
    ntrip_client_request_connect(&s_ntrip_client);
    message_queue_init(&s_tx_queue, s_tx_storage, sizeof(rtcm_router_tx_item_t), RTCM_ROUTER_TX_QUEUE_CAPACITY);
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

bool rtcm_router_pop_uart_tx(rtcm_router_tx_item_t* out_item)
{
    return message_queue_pop(&s_tx_queue, out_item);
}
