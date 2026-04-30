/* ========================================================================
 * aog_navigation_app.c — AOG/AgIO GPS & Heading Output (NAV-AOG-001)
 *
 * Prepares PGN 214 (combined GPS + Heading) from GNSS and heading snapshots.
 * Sends Hello Response only on request (PGN 253).
 *
 * PGN 214 is pushed to the AOG TX ring buffer every aog_send_interval_ms.
 * Transport (UDP) is handled by transport_udp — this component only
 * encodes PGN data and writes to the ring buffer.
 *
 * HARD RULES:
 *   - No direct physical network send calls
 *   - No UART I/O
 *   - PGN encoding delegated to protocol_aog / aog_pgn.h
 *   - No steering logic
 * ======================================================================== */

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

static void push_aog_pgn(aog_nav_app_t* app, uint16_t pgn,
                          const uint8_t* data, uint8_t data_len)
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

/* ---- Internal: build PGN 214 from GNSS + heading snapshots ---- */

static void build_pgn214(aog_nav_app_t* app)
{
    aog_pgn214_t pgn;
    memset(&pgn, 0, sizeof(pgn));

    /* ---- Default: all fields use sentinel values ---- */
    pgn.longitude        = AOG_PGN214_SENTINEL_DOUBLE;
    pgn.latitude         = AOG_PGN214_SENTINEL_DOUBLE;
    pgn.altitude         = AOG_PGN214_SENTINEL_FLOAT;
    pgn.heading_dual     = AOG_PGN214_SENTINEL_FLOAT;
    pgn.heading_true     = AOG_PGN214_SENTINEL_FLOAT;
    pgn.speed            = AOG_PGN214_SENTINEL_FLOAT;
    pgn.roll             = AOG_PGN214_SENTINEL_FLOAT;
    pgn.satellites       = AOG_PGN214_SENTINEL_UINT16;
    pgn.hdop_x100        = AOG_PGN214_SENTINEL_UINT16;
    pgn.age_x100         = AOG_PGN214_SENTINEL_UINT16;
    pgn.imu_heading_x10  = AOG_PGN214_SENTINEL_UINT16;
    pgn.imu_roll_x10     = AOG_PGN214_SENTINEL_INT16;
    pgn.imu_pitch        = AOG_PGN214_SENTINEL_INT16;
    pgn.imu_yaw_rate     = AOG_PGN214_SENTINEL_INT16;
    pgn.fix_quality      = AOG_PGN214_SENTINEL_FIX;

    /* ---- Fill position from GNSS primary snapshot ---- */
    if (app->position_source != NULL && snapshot_buffer_is_valid(app->position_source)) {
        nmea_gga_t gga;
        if (snapshot_buffer_get(app->position_source, &gga)) {
            if (gga.fix_quality > 0) {
                app->position_valid = true;
                pgn.longitude  = gga.longitude;
                pgn.latitude   = gga.latitude;
                pgn.altitude   = (float)gga.altitude;
                pgn.satellites = (uint16_t)gga.num_sats;
                pgn.fix_quality = (uint8_t)gga.fix_quality;

                /* HDOP: scale 0.01 → value = hdop * 100 */
                pgn.hdop_x100 = (uint16_t)(gga.hdop * 100.0f + 0.5f);

                /* Correction age: scale 0.01 */
                if (gga.age_diff_valid) {
                    pgn.age_x100 = (uint16_t)(gga.age_diff * 100.0f + 0.5f);
                }
            }
        }
    }

    /* ---- Fill heading from dual-antenna heading snapshot ---- */
    if (app->heading_source != NULL && snapshot_buffer_is_valid(app->heading_source)) {
        gnss_heading_snapshot_t hdg;
        if (snapshot_buffer_get(app->heading_source, &hdg)) {
            if (hdg.valid) {
                app->heading_valid = true;
                /* heading_deg → radians for PGN 214 */
                pgn.heading_dual = (float)(hdg.heading_deg * M_PI / 180.0);
            }
        }
    }

    /* ---- IMU fields: all sentinel (not available in this task) ---- */

    /* ---- Encode and push ---- */
    uint8_t data_buf[AOG_PGN214_DATA_SIZE];
    aog_pgn_encode_214(data_buf, &pgn);
    push_aog_pgn(app, AOG_PGN_214, data_buf, AOG_PGN214_DATA_SIZE);
    app->pgn214_sent_count++;
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

    /* ---- 3. PGN 214 Output at configured rate ---- */
    if (now_ms - app->last_aog_send_ms >= app->aog_send_interval_ms) {
        app->last_aog_send_ms = now_ms;

        /* Reset validity flags each cycle */
        app->position_valid = false;
        app->heading_valid = false;

        build_pgn214(app);
    }
}

runtime_component_t* aog_nav_app_get_component(aog_nav_app_t* app)
{
    if (app == NULL) {
        return NULL;
    }
    return &app->component;
}
