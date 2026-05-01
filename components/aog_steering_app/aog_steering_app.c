/* aog_steering_app.c — AOG Steering Application Component.
 *
 * Reads incoming AOG frames from the RX byte ring buffer, parses
 * PGN 252 (steering input) and PGN 253 (hello request), and produces:
 *   1. SteeringInput snapshot for downstream steering_control
 *   2. Periodic steering status output (PGN 252 status) via TX buffer
 *   3. Hello response on request
 */

#include "aog_steering_app.h"
#include <string.h>

/* ---- Helper: decode steering input from raw parser data ---- */

static void decode_steer_input(const uint8_t* data, uint8_t data_len,
                                aog_steer_input_t* input)
{
    if (data == NULL || input == NULL) {
        return;
    }

    if (data_len < AOG_STEER_DATA_SIZE) {
        return;
    }

    memcpy(&input->speed_ms, &data[0], 4);
    memcpy(&input->steer_angle_deg, &data[4], 4);
}

/* ---- Helper: encode steering status into raw bytes ---- */

static void encode_steer_status(uint8_t* buf, const aog_steer_status_t* status)
{
    if (buf == NULL || status == NULL) {
        return;
    }

    memcpy(buf, &status->steer_angle_actual_deg, 4);
    buf[4] = status->status;
    buf[5] = status->flags;
    buf[6] = 0;
    buf[7] = 0;
}

/* ---- Helper: encode AOG frame and write to TX destination ---- */

static void push_aog_pgn(aog_steering_app_t* app, uint16_t pgn,
                          const uint8_t* data, uint8_t data_len)
{
    if (app == NULL) {
        return;
    }

    uint8_t frame[AOG_MAX_FRAME_SIZE];
    size_t frame_len = aog_frame_encode(frame, app->src_address, pgn, data, data_len);
    if (frame_len > 0 && app->aog_tx_dest != NULL) {
        byte_ring_buffer_write(app->aog_tx_dest, frame, frame_len);
    }
}

/* ---- Public API ---- */

void aog_steering_app_init(aog_steering_app_t* app)
{
    if (app == NULL) {
        return;
    }

    memset(app, 0, sizeof(aog_steering_app_t));

    app->src_address = AOG_SRC_STEER;   /* default: steering source (0x07) */

    aog_parser_init(&app->aog_parser);

    /* Init the SteeringInput snapshot for downstream consumers. */
    snapshot_buffer_init(&app->steer_input_snapshot,
                         &app->steer_input_storage,
                         sizeof(aog_steer_input_t));

    app->component.service_step = aog_steering_app_service_step;
}

void aog_steering_app_set_aog_rx_source(aog_steering_app_t* app,
                                         byte_ring_buffer_t* source)
{
    if (app == NULL) {
        return;
    }
    app->aog_rx_source = source;
}

void aog_steering_app_set_aog_tx_dest(aog_steering_app_t* app,
                                       byte_ring_buffer_t* dest)
{
    if (app == NULL) {
        return;
    }
    app->aog_tx_dest = dest;
}

void aog_steering_app_set_src_address(aog_steering_app_t* app, uint8_t src)
{
    if (app == NULL) {
        return;
    }
    app->src_address = src;
}

void aog_steering_app_service_step(runtime_component_t* comp,
                                    uint64_t timestamp_us)
{
    aog_steering_app_t* app = (aog_steering_app_t*)comp;
    if (app == NULL) {
        return;
    }

    app->cycle_count++;

    /* ---- 1. Read incoming AOG frames, feed to parser ---- */
    if (app->aog_rx_source != NULL) {
        uint8_t rx_tmp[64];
        size_t rx_avail = byte_ring_buffer_available(app->aog_rx_source);
        if (rx_avail > 0) {
            size_t to_read = rx_avail > sizeof(rx_tmp) ? sizeof(rx_tmp) : rx_avail;
            size_t pulled = byte_ring_buffer_read(app->aog_rx_source, rx_tmp, to_read);
            for (size_t i = 0; i < pulled; i++) {
                bool frame_ready = aog_parser_feed(&app->aog_parser, rx_tmp[i]);
                if (frame_ready && app->aog_parser.crc_valid) {

                    /* ---- 2a. PGN 252: Steering Input from AgOpenGPS ---- */
                    if (app->aog_parser.pgn == AOG_PGN_STEER_INPUT) {
                        decode_steer_input(app->aog_parser.data,
                                           (uint8_t)app->aog_parser.data_count,
                                           &app->steer_input);
                        app->steer_input_valid = true;

                        /* Publish to SteeringInput snapshot for steering_control. */
                        snapshot_buffer_set(&app->steer_input_snapshot,
                                            &app->steer_input);
                    }

                    /* ---- 2b. PGN 253: Hello Request from AgOpenGPS ---- */
                    if (app->aog_parser.pgn == AOG_PGN_HELLO_REQUEST) {
                        app->hello_response_pending = true;
                    }
                }
            }
        }
    }

    /* ---- 3. Hello/Discovery Response (on request) ---- */
    if (app->hello_response_pending) {
        app->hello_response_pending = false;

        aog_hello_t hello;
        memset(&hello, 0, sizeof(hello));
        hello.ip[0] = 192;
        hello.ip[1] = 168;
        hello.ip[2] = 1;
        hello.ip[3] = 100;
        hello.port = 9999;
        hello.subnet_index = 0;

        uint8_t hello_buf[AOG_HELLO_DATA_SIZE];
        aog_pgn_encode_hello(hello_buf, &hello);
        push_aog_pgn(app, AOG_PGN_HELLO_RESPONSE, hello_buf,
                     (uint8_t)AOG_HELLO_DATA_SIZE);
    }

    /* ---- 4. Periodic steering status output (every 50ms = 20 Hz) ---- */
    uint64_t interval_us = (uint64_t)AOG_STEER_STATUS_INTERVAL_MS * 1000ULL;
    if (timestamp_us - app->last_status_send_us >= interval_us) {
        app->last_status_send_us = timestamp_us;

        uint8_t status_buf[AOG_STEER_STATUS_DATA_SIZE];
        encode_steer_status(status_buf, &app->steer_status);
        push_aog_pgn(app, AOG_PGN_STEER_STATUS, status_buf,
                     (uint8_t)AOG_STEER_STATUS_DATA_SIZE);
    }
}

runtime_component_t* aog_steering_app_get_component(aog_steering_app_t* app)
{
    if (app == NULL) {
        return NULL;
    }
    return &app->component;
}

const snapshot_buffer_t* aog_steering_app_get_steer_input_snapshot(
    const aog_steering_app_t* app)
{
    if (app == NULL) {
        return NULL;
    }
    return &app->steer_input_snapshot;
}
