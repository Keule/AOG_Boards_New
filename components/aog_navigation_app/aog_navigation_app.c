#include "aog_navigation_app.h"

#include "gnss_um980.h"
#include "gnss_dual_heading.h"
#include "protocol_aog.h"
#include "message_queue.h"

#define AOG_NAV_TX_QUEUE_CAPACITY 32U

static message_queue_t s_tx_queue;
static aog_navigation_tx_item_t s_tx_storage[AOG_NAV_TX_QUEUE_CAPACITY];
static bool s_hello_request_pending = false;

static void queue_aog_frame(aog_navigation_tx_kind_t kind, const aog_frame_t* frame)
{
    aog_navigation_tx_item_t item;
    size_t encoded_size = 0;

    if (frame == 0) {
        return;
    }

    if (!aog_encode_frame(frame, item.payload, sizeof(item.payload), &encoded_size)) {
        return;
    }

    item.kind = kind;
    item.length = encoded_size;
    message_queue_push(&s_tx_queue, &item);
}

static void aog_navigation_fast_output(runtime_component_t* component, const fast_cycle_context_t* ctx)
{
    const gnss_um980_receiver_data_t* primary = gnss_um980_primary();
    const gnss_dual_heading_data_t* heading = gnss_dual_heading_get();
    aog_frame_t frame;

    (void)component;
    (void)ctx;

    if (s_hello_request_pending) {
        aog_version_t version = {1, 0, 0};
        if (aog_build_discovery_response(&version, &frame)) {
            queue_aog_frame(AOG_NAV_TX_DISCOVERY, &frame);
        }
        s_hello_request_pending = false;
    }

    if (primary->valid) {
        aog_position_t position;
        position.latitude_e7 = primary->latitude_e7;
        position.longitude_e7 = primary->longitude_e7;
        position.altitude_mm = primary->altitude_mm;

        if (aog_build_position_out(&position, &frame)) {
            queue_aog_frame(AOG_NAV_TX_POSITION, &frame);
        }
    }

    if (heading->valid) {
        aog_heading_t h;
        h.heading_mdeg = heading->heading_mdeg;

        if (aog_build_heading_out(&h, &frame)) {
            queue_aog_frame(AOG_NAV_TX_HEADING, &frame);
        }
    }
}

static runtime_component_t s_component = {
    .name = "aog_navigation_app",
    .user_data = 0,
    .fast_input = 0,
    .fast_process = 0,
    .fast_output = aog_navigation_fast_output,
};

int aog_navigation_app_init(void)
{
    message_queue_init(&s_tx_queue, s_tx_storage, sizeof(aog_navigation_tx_item_t), AOG_NAV_TX_QUEUE_CAPACITY);
    s_hello_request_pending = false;
    return 0;
}

runtime_component_t* aog_navigation_app_component(void)
{
    return &s_component;
}

bool aog_navigation_app_push_rx_frame(const aog_frame_t* frame)
{
    if (frame == 0) {
        return false;
    }

    if (aog_is_hello_request(frame)) {
        s_hello_request_pending = true;
        return true;
    }

    return false;
}

bool aog_navigation_app_pop_tx(aog_navigation_tx_item_t* out_item)
{
    return message_queue_pop(&s_tx_queue, out_item);
}
