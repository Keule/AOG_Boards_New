#include "aog_navigation_app.h"

#include "gnss_um980.h"
#include "gnss_dual_heading.h"
#include "protocol_aog.h"
#include "transport_udp.h"

static void aog_navigation_fast_output(runtime_component_t* component, const fast_cycle_context_t* ctx)
{
    const gnss_um980_receiver_data_t* primary = gnss_um980_primary();
    const gnss_dual_heading_data_t* heading = gnss_dual_heading_get();
    aog_version_t version = {1, 0, 0};
    aog_frame_t frame;
    uint8_t encoded[256];
    size_t encoded_size = 0;
    size_t sent = 0;

    (void)component;
    (void)ctx;

    if (aog_build_discovery_response(&version, &frame) &&
        aog_encode_frame(&frame, encoded, sizeof(encoded), &encoded_size)) {
        transport_udp_send(encoded, encoded_size, &sent);
    }

    if (primary->valid) {
        aog_position_t position;
        position.latitude_e7 = primary->latitude_e7;
        position.longitude_e7 = primary->longitude_e7;
        position.altitude_mm = primary->altitude_mm;

        if (aog_build_position_out(&position, &frame) &&
            aog_encode_frame(&frame, encoded, sizeof(encoded), &encoded_size)) {
            transport_udp_send(encoded, encoded_size, &sent);
        }
    }

    if (heading->valid) {
        aog_heading_t h;
        h.heading_mdeg = heading->heading_mdeg;

        if (aog_build_heading_out(&h, &frame) &&
            aog_encode_frame(&frame, encoded, sizeof(encoded), &encoded_size)) {
            transport_udp_send(encoded, encoded_size, &sent);
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
    return transport_udp_init();
}

runtime_component_t* aog_navigation_app_component(void)
{
    return &s_component;
}
