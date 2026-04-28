#include "aog_steering_app.h"

#include <string.h>

static void handle_frame(aog_steering_app_t* app, uint64_t timestamp_us)
{
    steering_input_t input;

    if (!app->parser.crc_valid) {
        return;
    }

    if (app->parser.pgn == AOG_PGN_STEERING_INPUT && app->parser.data_count >= 2) {
        int16_t raw_target = (int16_t)((((uint16_t)app->parser.data[1]) << 8) | app->parser.data[0]);

        memset(&input, 0, sizeof(input));
        input.valid = true;
        input.target_angle_deg = ((float)raw_target) / 100.0f;
        input.timestamp_us = timestamp_us;
        steering_control_set_input(app->control, &input);
    }
}

static void publish_status(aog_steering_app_t* app)
{
    uint8_t payload[2];
    uint8_t frame[AOG_MAX_FRAME_SIZE];
    size_t frame_len;

    payload[0] = safety_failsafe_is_active(app->safety) ? 1U : 0U;
    payload[1] = safety_failsafe_outputs_high_z(app->safety) ? 1U : 0U;

    frame_len = aog_frame_encode(frame, AOG_PGN_STEERING_STATUS, payload, sizeof(payload));
    if (frame_len > 0) {
        (void)transport_udp_tx_write(app->udp, frame, frame_len);
    }
}

void aog_steering_app_init(aog_steering_app_t* app, transport_udp_t* udp, steering_control_t* control, safety_failsafe_t* safety)
{
    if (app == NULL) {
        return;
    }

    memset(app, 0, sizeof(*app));
    app->component.name = "aog_steering_app";
    app->component.user_data = app;
    app->component.service_step = aog_steering_app_service_step;
    app->udp = udp;
    app->control = control;
    app->safety = safety;
    aog_parser_init(&app->parser);
}

void aog_steering_app_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    aog_steering_app_t* app;
    uint8_t in[64];
    size_t read_len;
    size_t i;

    if (comp == NULL || comp->user_data == NULL) {
        return;
    }

    app = (aog_steering_app_t*)comp->user_data;
    read_len = transport_udp_rx_read(app->udp, in, sizeof(in));

    for (i = 0; i < read_len; ++i) {
        if (aog_parser_feed(&app->parser, in[i])) {
            handle_frame(app, timestamp_us);
        }
    }

    publish_status(app);
}
