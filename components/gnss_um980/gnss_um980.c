/* ========================================================================
 * gnss_um980.c — UM980 GNSS Receiver Implementation (NAV-GNSS-VALID-001)
 *
 * VALIDITY MODEL (Variant A):
 *   position_valid = fresh GGA with fix_quality in {1,2,3,4,5}
 *   motion_valid   = fresh RMC with status_valid == true
 *   accuracy_valid = fresh GST received
 *   valid          = position_valid AND motion_valid
 *   fresh          = valid AND all required data within timeout
 *
 * NAV-UM980-LOG-SYNTAX-RATE-FIX-001 additions:
 *   - GSA/GSV per-sentence counters
 *   - Last sentence type/age tracking
 *   - NMEA rate measurement with sliding window
 * ======================================================================== */

#include "gnss_um980.h"
#include <string.h>

/* ---- Internal: check if GGA fix_quality represents any valid fix ---- */
static bool gga_has_valid_fix(const nmea_gga_t* gga)
{
    if (gga == NULL) return false;
    /* 0 = no fix, 1-5 = various fix types, 6+ = undefined */
    return (gga->fix_quality >= 1 && gga->fix_quality <= 5);
}

/* ---- Internal: rebuild snapshot from dirty parsed data ---- */
static void gnss_um980_rebuild_snapshot(gnss_um980_t* rx, uint64_t timestamp_ms)
{
    if (rx == NULL) {
        return;
    }

    gnss_snapshot_t* snap = &rx->snapshot;

    /* ---- Update position from GGA ---- */
    if (rx->gga_dirty && rx->gga_valid) {
        snap->latitude    = rx->gga.latitude;
        snap->longitude   = rx->gga.longitude;
        snap->altitude    = rx->gga.altitude;
        snap->satellites  = rx->gga.num_sats;
        snap->hdop        = rx->gga.hdop;
        snap->fix_quality = gnss_fix_quality_from_gga(rx->gga.fix_quality);
        snap->rtk_status  = gnss_rtk_status_from_gga(rx->gga.fix_quality);
        snap->correction_age_valid = rx->gga.age_diff_valid;
        snap->correction_age_s     = rx->gga.age_diff_valid ? rx->gga.age_diff : 0.0;
        snap->last_gga_time_ms = timestamp_ms;

        /* Position validity depends on fix quality */
        if (gga_has_valid_fix(&rx->gga)) {
            snap->position_valid = true;
            snap->status_reason = GNSS_REASON_NONE;
        } else {
            snap->position_valid = false;
            if (rx->gga.fix_quality == 0) {
                snap->status_reason = GNSS_REASON_NO_FIX;
            } else {
                snap->status_reason = GNSS_REASON_UNKNOWN_FIX;
            }
        }

        rx->gga_dirty = false;

        /* Publish GGA to position snapshot buffer for consumers */
        if (snap->position_valid) {
            snapshot_buffer_set(&rx->position_snapshot, &rx->gga);
        }
    }
    if (rx->rmc_dirty && rx->rmc_valid) {
        snap->speed_ms   = gnss_knots_to_ms(rx->rmc.speed_knots);
        snap->course_deg = rx->rmc.course_true;
        snap->last_rmc_time_ms = timestamp_ms;

        /* Motion validity requires RMC status A (active) */
        if (rx->rmc.status_valid) {
            snap->motion_valid = true;
            if (snap->status_reason == GNSS_REASON_NONE ||
                snap->status_reason == GNSS_REASON_RMC_VOID ||
                snap->status_reason == GNSS_REASON_NO_RMC) {
                snap->status_reason = GNSS_REASON_NONE;
            }
        } else {
            snap->motion_valid = false;
            snap->status_reason = GNSS_REASON_RMC_VOID;
        }

        rx->rmc_dirty = false;
    }

    /* ---- Update accuracy from GST ---- */
    if (rx->gst_dirty && rx->gst_valid) {
        snap->std_lat = rx->gst.std_lat;
        snap->std_lon = rx->gst.std_lon;
        snap->std_alt = rx->gst.std_alt;
        snap->last_gst_time_ms = timestamp_ms;
        snap->accuracy_valid = true;
        rx->gst_dirty = false;
    }

    /* ---- Compute composite valid ---- */
    snap->valid = snap->position_valid && snap->motion_valid;

    /* ---- Copy cumulative statistics ---- */
    snap->gga_count        = rx->gga_count;
    snap->rmc_count        = rx->rmc_count;
    snap->gst_count        = rx->gst_count;
    snap->sentences_parsed = rx->sentences_parsed;
    snap->sentences_error  = rx->checksum_errors + rx->overflow_errors;
    snap->bytes_received   = rx->bytes_received;
    snap->timestamp_ms     = timestamp_ms;

    /* Fresh mark: just rebuilt, run freshness check */
    gnss_snapshot_check_freshness(snap, timestamp_ms, 0 /* use default */);
}

/* ---- Public API ---- */

void gnss_um980_init(gnss_um980_t* rx, uint8_t instance_id, const char* name)
{
    if (rx == NULL) {
        return;
    }

    memset(rx, 0, sizeof(gnss_um980_t));
    rx->instance_id = instance_id;
    rx->name = name;
    rx->freshness_timeout_ms = GNSS_FRESHNESS_TIMEOUT_MS_DEFAULT;

    nmea_parser_init(&rx->nmea_parser);
    gnss_snapshot_init(&rx->snapshot);
    snapshot_buffer_init(&rx->position_snapshot, &rx->position_storage,
                         sizeof(nmea_gga_t));

    /* Register service step callback */
    rx->component.service_step = gnss_um980_service_step;
}

void gnss_um980_set_rx_source(gnss_um980_t* rx, byte_ring_buffer_t* source)
{
    if (rx == NULL) {
        return;
    }
    rx->rx_source = source;
}

void gnss_um980_set_freshness_timeout(gnss_um980_t* rx, uint32_t timeout_ms)
{
    if (rx == NULL) {
        return;
    }
    if (timeout_ms == 0) {
        timeout_ms = GNSS_FRESHNESS_TIMEOUT_MS_DEFAULT;
    }
    if (timeout_ms < GNSS_FRESHNESS_TIMEOUT_MS_MIN) {
        timeout_ms = GNSS_FRESHNESS_TIMEOUT_MS_MIN;
    }
    if (timeout_ms > GNSS_FRESHNESS_TIMEOUT_MS_MAX) {
        timeout_ms = GNSS_FRESHNESS_TIMEOUT_MS_MAX;
    }
    rx->freshness_timeout_ms = timeout_ms;
}

uint32_t gnss_um980_feed(gnss_um980_t* rx, const uint8_t* data, size_t length)
{
    if (rx == NULL || data == NULL || length == 0) {
        return 0;
    }

    uint32_t sentences_parsed = 0;
    rx->bytes_received += (uint32_t)length;

    for (size_t i = 0; i < length; i++) {
        nmea_result_t result = nmea_parser_feed(&rx->nmea_parser, data[i]);

        if (result == NMEA_RESULT_VALID) {
            sentences_parsed++;

            switch (rx->nmea_parser.type) {
            case NMEA_SENTENCE_GGA:
                rx->gga = rx->nmea_parser.data.gga;
                rx->gga_valid = true;
                rx->gga_count++;
                rx->gga_dirty = true;
                break;

            case NMEA_SENTENCE_RMC:
                rx->rmc = rx->nmea_parser.data.rmc;
                rx->rmc_valid = true;
                rx->rmc_count++;
                rx->rmc_dirty = true;
                break;

            case NMEA_SENTENCE_GST:
                rx->gst = rx->nmea_parser.data.gst;
                rx->gst_valid = true;
                rx->gst_count++;
                rx->gst_dirty = true;
                break;

            case NMEA_SENTENCE_GSA:
                rx->gsa_count++;
                break;

            case NMEA_SENTENCE_GSV:
                rx->gsv_count++;
                break;

            default:
                break;
            }

            /* Track last sentence type and timestamp */
            rx->last_sentence_type = rx->nmea_parser.type;
        } else if (result == NMEA_RESULT_INVALID_CHECKSUM) {
            rx->checksum_errors++;
            rx->snapshot.last_error = GNSS_ERR_CHECKSUM;
            /* CRITICAL: Invalid checksum → no data copied, no dirty flags set.
             * Existing snapshot fields remain unchanged. */
        } else if (result == NMEA_RESULT_OVERFLOW) {
            rx->overflow_errors++;
            rx->snapshot.last_error = GNSS_ERR_OVERFLOW;
        }
    }

    rx->sentences_parsed += sentences_parsed;

    /* Update last_sentence_us from service_step caller — not available here.
     * Set to 0 and let service_step stamp it. */
    if (sentences_parsed > 0) {
        rx->last_sentence_us = 0; /* will be set by service_step */
    }

    return sentences_parsed;
}

void gnss_um980_finalize_snapshot(gnss_um980_t* rx, uint64_t timestamp_ms)
{
    if (rx == NULL) {
        return;
    }
    gnss_um980_rebuild_snapshot(rx, timestamp_ms);
}

/* ---- NMEA rate measurement constants ---- */
#define GNSS_RATE_WINDOW_MS    5000    /* 5-second sliding window */
#define GNSS_RATE_MIN_WINDOW_MS 1000   /* minimum window for valid rate */

/* Rate thresholds for status flags */
#define GNSS_RATE_GGA_HIGH_HZ     30.0f
#define GNSS_RATE_RMC_HIGH_HZ     30.0f
#define GNSS_RATE_GST_HIGH_HZ     30.0f
#define GNSS_RATE_GSA_HIGH_HZ      5.0f
#define GNSS_RATE_GSV_ACTIVE_HZ    0.2f
#define GNSS_RATE_TOTAL_HIGH_HZ   90.0f
#define GNSS_RATE_DUPLICATE_HZ   120.0f

/* Rate guard: max auto-recovery attempts per boot */
#define GNSS_RATE_GUARD_MAX_RECOVERIES 1

/* ---- Internal: update NMEA rate measurement ---- */
static void gnss_um980_update_rate(gnss_um980_t* rx, uint64_t timestamp_ms)
{
    if (rx == NULL) return;

    uint32_t counts[5] = {
        rx->gga_count,
        rx->rmc_count,
        rx->gst_count,
        rx->gsa_count,
        rx->gsv_count
    };

    rx->nmea_rate.curr_time_ms = timestamp_ms;

    if (rx->nmea_rate.prev_time_ms == 0) {
        /* First sample — just store baseline */
        memcpy(rx->nmea_rate.prev_counts, counts, sizeof(counts));
        rx->nmea_rate.prev_time_ms = timestamp_ms;
        rx->nmea_rate.warmup = true;
        return;
    }

    uint64_t delta_ms = timestamp_ms - rx->nmea_rate.prev_time_ms;

    /* Only compute rate if window is large enough */
    if (delta_ms < GNSS_RATE_MIN_WINDOW_MS) {
        return;
    }

    rx->nmea_rate.window_ms = (uint32_t)delta_ms;

    /* rate_hz = (count_now - count_prev) * 1000.0 / delta_ms */
    float total = 0.0f;
    for (int i = 0; i < 5; i++) {
        uint32_t delta_count = counts[i] - rx->nmea_rate.prev_counts[i];
        float hz = (delta_ms > 0) ? ((float)delta_count * 1000.0f / (float)delta_ms) : 0.0f;
        rx->nmea_rate.rates_hz[i] = hz;
        total += hz;
    }
    rx->nmea_rate.total_hz = total;

    /* Update status flags */
    rx->nmea_rate.rate_high = (
        rx->nmea_rate.rates_hz[0] > GNSS_RATE_GGA_HIGH_HZ ||
        rx->nmea_rate.rates_hz[1] > GNSS_RATE_RMC_HIGH_HZ ||
        rx->nmea_rate.rates_hz[2] > GNSS_RATE_GST_HIGH_HZ ||
        rx->nmea_rate.rates_hz[3] > GNSS_RATE_GSA_HIGH_HZ
    );
    rx->nmea_rate.gsv_active = (rx->nmea_rate.rates_hz[4] > GNSS_RATE_GSV_ACTIVE_HZ);
    rx->nmea_rate.duplicate_suspected = (total > GNSS_RATE_DUPLICATE_HZ);

    /* Slide window */
    memcpy(rx->nmea_rate.prev_counts, counts, sizeof(counts));
    rx->nmea_rate.prev_time_ms = timestamp_ms;
    rx->nmea_rate.warmup = false;
    rx->nmea_rate.sample_count++;
}

void gnss_um980_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    gnss_um980_t* rx = (gnss_um980_t*)comp;
    if (rx == NULL) {
        return;
    }

    uint64_t ts_ms = timestamp_us / 1000;
    rx->last_service_ms = ts_ms;

    /* No RX source → just check freshness */
    if (rx->rx_source == NULL) {
        gnss_snapshot_check_freshness(&rx->snapshot, ts_ms, rx->freshness_timeout_ms);
        if (!rx->snapshot.fresh && rx->snapshot.valid) {
            rx->timeout_events++;
        }
        return;
    }

    /* Consume bytes from RX source buffer
     * If nmea_config is intercepting, it handles RX → feed.
     * gnss_um980_service_step skips direct reads in that case. */
    if (!rx->nmea_config_intercept) {
        /* Normal mode: read from UART RX buffer directly */
        uint8_t tmp[64];
        size_t available = byte_ring_buffer_available(rx->rx_source);

        if (available > 0) {
            size_t to_read = available > sizeof(tmp) ? sizeof(tmp) : available;
            size_t pulled = byte_ring_buffer_read(rx->rx_source, tmp, to_read);
            if (pulled > 0) {
                uint32_t before = rx->sentences_parsed;
                gnss_um980_feed(rx, tmp, pulled);
                if (rx->sentences_parsed > before) {
                    rx->last_sentence_us = timestamp_us;
                }
            }
        }
    }
    /* else: nmea_config handles RX → feed, nothing to do here */

    /* Rebuild snapshot if any sentence type was updated */
    if (rx->gga_dirty || rx->rmc_dirty || rx->gst_dirty) {
        gnss_um980_rebuild_snapshot(rx, ts_ms);
    }

    /* Always check freshness (even when no new data) */
    bool was_fresh = rx->snapshot.fresh;
    gnss_snapshot_check_freshness(&rx->snapshot, ts_ms, rx->freshness_timeout_ms);

    /* Count timeout transitions (fresh → stale) */
    if (was_fresh && !rx->snapshot.fresh && rx->snapshot.valid) {
        rx->timeout_events++;
    }

    /* Update NMEA rate measurement every service step */
    gnss_um980_update_rate(rx, ts_ms);
}

const nmea_gga_t* gnss_um980_get_gga(const gnss_um980_t* rx)
{
    if (rx == NULL || !rx->gga_valid) {
        return NULL;
    }
    return &rx->gga;
}

const nmea_rmc_t* gnss_um980_get_rmc(const gnss_um980_t* rx)
{
    if (rx == NULL || !rx->rmc_valid) {
        return NULL;
    }
    return &rx->rmc;
}

const nmea_gst_t* gnss_um980_get_gst(const gnss_um980_t* rx)
{
    if (rx == NULL || !rx->gst_valid) {
        return NULL;
    }
    return &rx->gst;
}

const gnss_snapshot_t* gnss_um980_get_snapshot(const gnss_um980_t* rx)
{
    if (rx == NULL) {
        return NULL;
    }
    return &rx->snapshot;
}

bool gnss_um980_has_fix(const gnss_um980_t* rx)
{
    if (rx == NULL) {
        return false;
    }
    return rx->snapshot.position_valid;
}

bool gnss_um980_is_fresh(const gnss_um980_t* rx)
{
    if (rx == NULL) {
        return false;
    }
    return rx->snapshot.fresh;
}

const snapshot_buffer_t* gnss_um980_get_position_snapshot(const gnss_um980_t* rx)
{
    if (rx == NULL) {
        return NULL;
    }
    return &rx->position_snapshot;
}
