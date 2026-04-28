#include "aog_navigation_app.h"

#include <string.h>

static bool is_discovery_request(const aog_frame_t* frame)
{
    return frame != 0 && frame->type == AOG_FRAME_TYPE_DISCOVERY_REQUEST;
}

static void push_discovery_response(aog_navigation_app_t* app)
{
    aog_frame_t out;
    memset(&out, 0, sizeof(out));

    out.type = AOG_FRAME_TYPE_DISCOVERY_RESPONSE;
    out.seq = app->next_sequence++;
    out.payload[0] = 0x01U;
    out.length = 1;
    message_queue_push(&app->tx_queue, &out);
}

static void push_position_heading(aog_navigation_app_t* app)
{
    gnss_snapshot_t gnss;
    heading_snapshot_t heading;
    aog_frame_t out;
    int32_t lat_scaled = 0;
    int32_t lon_scaled = 0;

    if (!gnss_um980_get_snapshot(app->primary, &gnss)) {
        return;
    }

    memset(&out, 0, sizeof(out));
    out.type = AOG_FRAME_TYPE_POSITION_HEADING;
    out.seq = app->next_sequence++;

    lat_scaled = (int32_t)(gnss.latitude_deg * 1000000.0);
    lon_scaled = (int32_t)(gnss.longitude_deg * 1000000.0);

    memcpy(&out.payload[0], &lat_scaled, sizeof(lat_scaled));
    memcpy(&out.payload[4], &lon_scaled, sizeof(lon_scaled));
    memcpy(&out.payload[8], &gnss.altitude_m, sizeof(gnss.altitude_m));

    out.length = 12;

    if (gnss_dual_heading_get_snapshot(app->heading, &heading)) {
        memcpy(&out.payload[12], &heading.heading_deg, sizeof(heading.heading_deg));
        out.length = 16;
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
    aog_frame_t in;
    if (app == 0) {
        return;
    }

    memset(&in, 0, sizeof(in));
    while (message_queue_pop(&app->rx_queue, &in)) {
        if (is_discovery_request(&in)) {
            push_discovery_response(app);
        }
    }

    push_position_heading(app);
}

runtime_component_t* aog_navigation_app_component(aog_navigation_app_t* app)
{
    return (app != 0) ? &app->component : 0;
}
