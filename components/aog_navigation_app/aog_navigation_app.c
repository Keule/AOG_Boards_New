#include "aog_navigation_app.h"
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- Constants ---- */

#define AOG_SEND_INTERVAL_MS    50   /* 20 Hz */
#define MS_TO_KMH_FACTOR        3.6  /* 1 m/s = 3.6 km/h */

/* ---- Helper: encode AOG v5 frame and write to TX destination ---- */

static void push_aog_pgn(aog_nav_app_t* app, uint16_t pgn,
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

/* ---- Helper: check heading freshness ----
 * Returns true if heading has been updated within the freshness timeout. */

static bool is_heading_fresh(const aog_nav_app_t* app, uint32_t now_ms)
{
    if (!app->heading_ever_seen) {
        return false;
    }
    if (now_ms < app->last_heading_update_ms) {
        return false;  /* clock wrap protection */
    }
    return (now_ms - app->last_heading_update_ms) < app->heading_freshness_ms;
}

/* ---- Helper: map gnss_fix_quality_t enum to raw GGA byte ---- */

static uint8_t fix_quality_to_raw(gnss_fix_quality_t fq)
{
    switch (fq) {
    case GNSS_FIX_SINGLE:    return 1;
    case GNSS_FIX_DGPS:      return 2;
    case GNSS_FIX_PPS:       return 3;
    case GNSS_FIX_RTK_FIXED: return 4;
    case GNSS_FIX_RTK_FLOAT: return 5;
    default:                 return 0;
    }
}

/* ---- Public API ---- */

void aog_nav_app_init(aog_nav_app_t* app)
{
    if (app == NULL) {
        return;
    }

    memset(app, 0, sizeof(aog_nav_app_t));

    aog_parser_init_ex(&app->aog_rx_parser, true);  /* tolerant CRC for Discovery */

    app->src_address = AOG_SRC_GPS;
    app->module_type = AOG_MODULE_TYPE_GPS;         /* 0x78 per PGN-Verzeichnis */
    app->aog_send_interval_ms = AOG_SEND_INTERVAL_MS;
    app->heading_freshness_ms = AOG_HEADING_FRESHNESS_MS_DEFAULT;
    app->output_state = AOG_OUTPUT_INIT;

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

void aog_nav_app_set_src_address(aog_nav_app_t* app, uint8_t src)
{
    if (app == NULL) return;
    app->src_address = src;
}

void aog_nav_app_set_heading_freshness(aog_nav_app_t* app, uint32_t timeout_ms)
{
    if (app == NULL) return;
    if (timeout_ms < AOG_HEADING_FRESHNESS_MS_MIN) {
        timeout_ms = AOG_HEADING_FRESHNESS_MS_MIN;
    }
    if (timeout_ms > AOG_HEADING_FRESHNESS_MS_MAX) {
        timeout_ms = AOG_HEADING_FRESHNESS_MS_MAX;
    }
    app->heading_freshness_ms = timeout_ms;
}

void aog_nav_app_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    aog_nav_app_t* app = (aog_nav_app_t*)comp;
    if (app == NULL) {
        return;
    }

    app->cycle_count++;
    uint32_t now_ms = (uint32_t)(timestamp_us / 1000);

    /* ==================================================================
     * 1. READ INCOMING AOG FRAMES (Hello + Scan Request detection)
     *    NAV-AOG-001-FINAL: Discovery frames use tolerant CRC
     * ================================================================== */
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
                    if (app->aog_rx_parser.pgn == AOG_PGN_SCAN_REQUEST) {
                        app->scan_response_pending = true;
                    }
                }
            }
        }
    }

    /* ==================================================================
     * 2. HELLO RESPONSE (PGN 254) — only on PGN 253 request
     *    Discovery responses bypass output gating (always emitted).
     * ================================================================== */
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
        app->hello_send_count++;
    }

    /* ==================================================================
     * 3. SCAN REPLY (PGN 203) — only on PGN 202 request
     *    NAV-AOG-001-FINAL: module_type = 0x78 (120) per PGN-Verzeichnis
     * ================================================================== */
    if (app->scan_response_pending) {
        app->scan_response_pending = false;

        aog_scan_reply_t scan;
        memset(&scan, 0, sizeof(scan));
        scan.src = app->src_address;
        scan.module_type = app->module_type;   /* 0x78 = GPS per PGN-Verzeichnis */
        scan.pgn_count = 2;
        scan.pgns[0] = AOG_PGN_214_OUT;       /* PGN 214 */
        scan.pgns[1] = AOG_PGN_HELLO_RESPONSE; /* PGN 254 */

        uint8_t scan_buf[4 + AOG_SCAN_REPLY_MAX_PGNS * 2];
        uint8_t scan_len = aog_pgn_encode_scan_reply(scan_buf, &scan);
        push_aog_pgn(app, AOG_PGN_SCAN_REPLY, scan_buf, scan_len);
        app->scan_send_count++;
    }

    /* ==================================================================
     * 4. PERIODIC PGN 214 OUTPUT AT CONFIGURED RATE
     * ================================================================== */
    if ((app->pgn214_send_count == 0) ||
        (now_ms - app->last_aog_send_ms >= app->aog_send_interval_ms)) {
        app->last_aog_send_ms = now_ms;

        /* ---- Allocate PGN 214 encode buffer ---- *
         * IMPORTANT: Never cast aog_pgn214_t directly to bytes!
         * The C struct has padding (u8 fix_quality + u16 hdop), but
         * the AOG wire format has NO padding. Always use the encoder.
         */
        uint8_t pgn_buf[AOG_PGN214_DATA_SIZE];

        /* ---- Read GNSS snapshot ---- */
        gnss_snapshot_t gnss;
        bool gnss_available = false;
        bool gnss_valid = false;
        bool gnss_fresh = false;

        if (app->position_source != NULL &&
            snapshot_buffer_is_valid(app->position_source)) {
            if (snapshot_buffer_get(app->position_source, &gnss)) {
                gnss_available = true;
                gnss_valid = gnss.valid;
                gnss_fresh = gnss.fresh;
            }
        }

        /* ---- Read Heading snapshot ---- */
        gnss_dual_heading_t hdg;
        bool hdg_available = false;
        bool hdg_valid = false;
        bool hdg_fresh = false;

        if (app->heading_source != NULL &&
            snapshot_buffer_is_valid(app->heading_source)) {
            if (snapshot_buffer_get(app->heading_source, &hdg)) {
                hdg_available = true;
                hdg_valid = hdg.valid;

                /* Track heading freshness via sequence number */
                uint32_t current_seq = snapshot_buffer_sequence(app->heading_source);
                if (current_seq != app->last_heading_sequence) {
                    app->last_heading_sequence = current_seq;
                    app->last_heading_update_ms = now_ms;
                    app->heading_ever_seen = true;
                }
                hdg_fresh = is_heading_fresh(app, now_ms);
            }
        }

        /* ==================================================================
         * 5. OUTPUT GATING — strict per NAV-AOG-001-FINAL
         *
         * P1: GNSS must be valid AND fresh → otherwise suppress/sentinel
         * P4: Heading fallback: lost vs invalid vs stale
         * P6: 8-state output model
         * ================================================================== */

        aog_pgn214_t pgn214;

        if (!gnss_available || !gnss_valid) {
            /* ---- GNSS INVALID: suppress GPS output ---- */
            app->output_state = AOG_OUTPUT_GNSS_INVALID;
            app->gnss_output_active = false;
            app->heading_output_active = false;
            app->suppress_count++;

            /* Send fully sentinel PGN 214 with NO FIX status */
            aog_pgn214_set_sentinels(&pgn214);
            pgn214.fix_quality = AOG_FIX_NONE;
            aog_pgn_encode_pgn214(pgn_buf, &pgn214);
            push_aog_pgn(app, AOG_PGN_214_OUT,
                        pgn_buf, AOG_PGN214_DATA_SIZE);
            app->pgn214_send_count++;

        } else if (!gnss_fresh) {
            /* ---- GNSS STALE: degraded, suppress GPS data ---- */
            app->output_state = AOG_OUTPUT_GNSS_STALE;
            app->gnss_output_active = false;
            app->heading_output_active = false;
            app->suppress_count++;

            /* Send sentinel PGN 214 — stale data must not be forwarded */
            aog_pgn214_set_sentinels(&pgn214);
            pgn214.fix_quality = AOG_FIX_NONE;
            aog_pgn_encode_pgn214(pgn_buf, &pgn214);
            push_aog_pgn(app, AOG_PGN_214_OUT,
                        pgn_buf, AOG_PGN214_DATA_SIZE);
            app->pgn214_send_count++;

        } else {
            /* ---- GNSS VALID + FRESH: build PGN 214 ---- */
            memset(&pgn214, 0, sizeof(pgn214));

            /* Position fields */
            pgn214.longitude  = gnss.longitude;
            pgn214.latitude   = gnss.latitude;
            pgn214.altitude   = (float)gnss.altitude;
            pgn214.satellites = (uint16_t)gnss.satellites;

            /* Fix quality mapping: gnss_fix_quality_t → AOG byte */
            pgn214.fix_quality = aog_fix_quality_to_aog(fix_quality_to_raw(gnss.fix_quality));

            /* HDOP: scale to ×100 */
            pgn214.hdop_x100 = (uint16_t)(gnss.hdop * 100.0 + 0.5f);
            if (gnss.hdop <= 0.0) {
                pgn214.hdop_x100 = AOG_SENTINEL_U16;
            }

            /* Correction age: scale to ×100 */
            if (gnss.correction_age_valid) {
                pgn214.age_x100 = (uint16_t)(gnss.correction_age_s * 100.0 + 0.5f);
            } else {
                pgn214.age_x100 = AOG_SENTINEL_U16;
            }

            /* Speed: m/s → km/h */
            pgn214.speed_kmh = (float)(gnss.speed_ms * MS_TO_KMH_FACTOR);

            /* Course over ground: degrees (from RMC) */
            pgn214.heading_true = (float)gnss.course_deg;

            /* ---- Heading gating (P4: Heading Fallback) ---- */
            if (!hdg_available) {
                /* NAV-AOG-001-FINAL: Heading source disappeared (HEADING_LOST) */
                pgn214.heading_dual = AOG_SENTINEL_FLOAT;
                app->heading_output_active = false;
                app->output_state = AOG_OUTPUT_HEADING_LOST;

            } else if (!hdg_valid) {
                /* Heading source reports invalid (HEADING_INVALID) */
                pgn214.heading_dual = AOG_SENTINEL_FLOAT;
                app->heading_output_active = false;
                app->output_state = AOG_OUTPUT_HEADING_INVALID;

            } else if (!hdg_fresh) {
                /* Heading was valid but expired (HEADING_STALE) */
                pgn214.heading_dual = AOG_SENTINEL_FLOAT;
                app->heading_output_active = false;
                app->output_state = AOG_OUTPUT_HEADING_STALE;

            } else {
                /* VALID + FRESH heading: dual antenna heading */
                pgn214.heading_dual = (float)(hdg.heading_rad * 180.0 / M_PI);
                if (pgn214.heading_dual < 0.0f) {
                    pgn214.heading_dual += 360.0f;
                }
                app->heading_output_active = true;
                app->output_state = AOG_OUTPUT_OK;
            }

            /* Roll: no IMU data available → sentinel */
            pgn214.roll = AOG_SENTINEL_FLOAT;

            /* IMU fields: no IMU → sentinels */
            pgn214.imu_heading_x10 = AOG_SENTINEL_U16;
            pgn214.imu_roll_x10    = AOG_SENTINEL_I16;
            pgn214.imu_pitch       = AOG_SENTINEL_I16;
            pgn214.imu_yaw_rate    = AOG_SENTINEL_I16;

            /* GNSS data is active */
            app->gnss_output_active = true;

            /* Encode and send PGN 214 */
            aog_pgn_encode_pgn214(pgn_buf, &pgn214);
            push_aog_pgn(app, AOG_PGN_214_OUT,
                        pgn_buf, AOG_PGN214_DATA_SIZE);
            app->pgn214_send_count++;
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

aog_output_state_t aog_nav_app_get_output_state(const aog_nav_app_t* app)
{
    if (app == NULL) {
        return AOG_OUTPUT_SUPPRESSED;
    }
    return app->output_state;
}
