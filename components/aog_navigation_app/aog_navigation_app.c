#include "aog_navigation_app.h"
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- Constants ---- */

#define AOG_SEND_INTERVAL_MS    50   /* 20 Hz */

/* PGN 253: Hello Request from AgOpenGPS (incoming) */
#ifndef AOG_PGN_HELLO_REQUEST
#define AOG_PGN_HELLO_REQUEST   253
#endif

/* ---- Helper: encode AOG frame and write to TX destination ---- */

static void push_aog_pgn(aog_nav_app_t* app, uint16_t pgn, const uint8_t* data, uint8_t data_len)
{
    if (app == NULL) {
        return;
    }

    uint8_t frame[AOG_MAX_FRAME_SIZE];
    size_t frame_len = aog_frame_encode(frame, pgn, data, data_len);
    if (frame_len > 0 && app->aog_tx_dest != NULL) {
        byte_ring_buffer_write(app->aog_tx_dest, frame, frame_len);
    }
}

/* ---- Public API ---- */

void aog_nav_app_init(aog_nav_app_t* app)
{
    if (app == NULL) {
        return;
    }

    memset(app, 0, sizeof(aog_nav_app_t));

    aog_parser_init(&app->aog_rx_parser);

    app->aog_send_interval_ms = AOG_SEND_INTERVAL_MS;

    app->component.service_step = aog_nav_app_service_step;
}

void aog_nav_app_set_position_source(aog_nav_app_t* app, const snapshot_buffer_t* source)
{
    if (app == NULL) return;
    app->position_source = source;
}

void aog_nav_app_set_heading_source(aog_nav_app_t* app, const snapshot_buffer_t* source)
{
    if (app == NULL) return;
    app->heading_source = source;
}

void aog_nav_app_set_aog_rx_source(aog_nav_app_t* app, byte_ring_buffer_t* source)
{
    if (app == NULL) return;
    app->aog_rx_source = source;
}

void aog_nav_app_set_aog_tx_dest(aog_nav_app_t* app, byte_ring_buffer_t* dest)
{
    if (app == NULL) return;
    app->aog_tx_dest = dest;
}

void aog_nav_app_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    aog_nav_app_t* app = (aog_nav_app_t*)comp;
    if (app == NULL) {
        return;
    }

    app->cycle_count++;
    uint32_t now_ms = (uint32_t)(timestamp_us / 1000);

    /* ---- 1. Read incoming AOG frames (Hello detection) ---- */
    if (app->aog_rx_source != NULL) {
        uint8_t rx_tmp[64];
        size_t rx_avail = byte_ring_buffer_available(app->aog_rx_source);
        if (rx_avail > 0) {
            size_t to_read = rx_avail > sizeof(rx_tmp) ? sizeof(rx_tmp) : rx_avail;
            size_t pulled = byte_ring_buffer_read(app->aog_rx_source, rx_tmp, to_read);
            for (size_t i = 0; i < pulled; i++) {
                bool frame_ready = aog_parser_feed(&app->aog_rx_parser, rx_tmp[i]);
                if (frame_ready && app->aog_rx_parser.crc_valid) {
                    if (app->aog_rx_parser.pgn == AOG_PGN_HELLO_REQUEST) {
                        app->hello_response_pending = true;
                    }
                }
            }
        }
    }

    /* ---- 2. Hello/Discovery Response (only on request) ---- */
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
        push_aog_pgn(app, AOG_PGN_HELLO_RESPONSE, hello_buf, AOG_HELLO_DATA_SIZE);
    }

    /* ---- 3. AOG Output at configured rate ---- */
    if (now_ms - app->last_aog_send_ms >= app->aog_send_interval_ms) {
        app->last_aog_send_ms = now_ms;

        /* Position output (PGN 200) — read from position snapshot */
        if (app->position_source != NULL && snapshot_buffer_is_valid(app->position_source)) {
            nmea_gga_t gga;
            if (snapshot_buffer_get(app->position_source, &gga)) {
                if (gga.fix_quality > 0) {
                    app->position.fix = gga.fix_quality;
                    app->position.num_sats = gga.num_sats;
                    app->position.latitude = gga.latitude;
                    app->position.longitude = gga.longitude;
                    app->position_valid = true;

                    uint8_t pos_buf[AOG_POSITION_DATA_SIZE];
                    aog_pgn_encode_position(pos_buf, &app->position);
                    push_aog_pgn(app, AOG_PGN_POSITION_OUT, pos_buf, AOG_POSITION_DATA_SIZE);
                }
            }
        }

        /* Heading output (PGN 201) — read from heading snapshot */
        if (app->heading_source != NULL && snapshot_buffer_is_valid(app->heading_source)) {
            gnss_heading_snapshot_t hdg;
            if (snapshot_buffer_get(app->heading_source, &hdg)) {
                if (hdg.valid) {
                    /* heading_deg → radians for AOG PGN 201 */
                    app->aog_heading.heading = hdg.heading_deg * M_PI / 180.0;
                    app->aog_heading.roll = 0.0;
                    app->heading_valid = true;

                    uint8_t hdg_buf[AOG_HEADING_DATA_SIZE];
                    aog_pgn_encode_heading(hdg_buf, &app->aog_heading);
                    push_aog_pgn(app, AOG_PGN_HEADING_OUT, hdg_buf, AOG_HEADING_DATA_SIZE);
                }
            }
        }
    }
}

runtime_component_t* aog_nav_app_get_component(aog_nav_app_t* app)
{
    if (app == NULL) {
        return NULL;
    }
    return &app->component;
}
