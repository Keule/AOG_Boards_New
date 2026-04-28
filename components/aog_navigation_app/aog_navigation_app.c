#include "aog_navigation_app.h"

#include <string.h>

static bool is_discovery_request(const aog_frame_t* frame)
{
    return (frame != 0 && frame->length > 0 && frame->data[0] == 0xA1U);
}

static void push_hello_response(aog_navigation_app_t* app)
{
    aog_frame_t out = {0};
    out.data[0] = 0xA2U;
    out.length = 1;
    message_queue_push(&app->tx_queue, &out);
}

static void push_position_heading(aog_navigation_app_t* app)
{
    gnss_snapshot_t gnss;
    heading_snapshot_t heading;
    aog_frame_t out = {0};

    if (!gnss_um980_get_snapshot(app->primary, &gnss)) {
        return;
    }

    out.data[0] = 0xB1U;
    out.length = 1;
    if (gnss_dual_heading_get_snapshot(app->heading, &heading)) {
        out.data[1] = (uint8_t)((int)heading.heading_deg & 0xFF);
        out.length = 2;
    }

    message_queue_push(&app->tx_queue, &out);
}

static void aog_navigation_component_step(runtime_component_t* component)
{
    aog_navigation_app_t* app = 0;
    if (component == 0) {
        return;
    }

    app = (aog_navigation_app_t*)component->user_data;
    aog_navigation_app_step(app);
}

void aog_navigation_app_init(aog_navigation_app_t* app, const gnss_um980_t* primary, const gnss_dual_heading_t* heading)
{
    if (app == 0) {
        return;
    }

    memset(app, 0, sizeof(*app));
    app->primary = primary;
    app->heading = heading;
    message_queue_init(&app->rx_queue, app->rx_storage, sizeof(aog_frame_t), 8);
    message_queue_init(&app->tx_queue, app->tx_storage, sizeof(aog_frame_t), 8);
    app->component.name = "aog_navigation_app";
    app->component.user_data = app;
    app->component.step = aog_navigation_component_step;
}

bool aog_navigation_app_feed_rx(aog_navigation_app_t* app, const aog_frame_t* frame)
{
    return (app != 0 && frame != 0) ? message_queue_push(&app->rx_queue, frame) : false;
}

bool aog_navigation_app_pop_tx(aog_navigation_app_t* app, aog_frame_t* out_frame)
{
    return (app != 0 && out_frame != 0) ? message_queue_pop(&app->tx_queue, out_frame) : false;
}

void aog_navigation_app_step(aog_navigation_app_t* app)
{
    aog_frame_t in = {0};
    if (app == 0) {
        return;
    }

    if (message_queue_pop(&app->rx_queue, &in) && is_discovery_request(&in)) {
        push_hello_response(app);
    }

    push_position_heading(app);
}

runtime_component_t* aog_navigation_app_component(aog_navigation_app_t* app)
{
    return (app != 0) ? &app->component : 0;
}
