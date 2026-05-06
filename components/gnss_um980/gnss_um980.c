/* ========================================================================
 * gnss_um980.c — UM980 GNSS Receiver Implementation (NAV-GNSS-VALID-001)
 *
 * VALIDITY MODEL (Variant A):
 *   position_valid = fresh GGA with fix_quality in {1,2,3,4,5}
 *   motion_valid   = fresh RMC with status_valid == true
 *   accuracy_valid = fresh GST received
 *   valid          = position_valid AND motion_valid
 *   fresh          = valid AND all required data within timeout
 * ======================================================================== */

#include "gnss_um980.h"
#include "esp_timer.h"
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
        snap->last_gga_time_ms = timestamp_ms;

        if (gga_has_valid_fix(&rx->gga)) {
            /* GOOD GGA: update position data AND set valid */
            snap->latitude    = rx->gga.latitude;
            snap->longitude   = rx->gga.longitude;
            snap->altitude    = rx->gga.altitude;
            snap->satellites  = rx->gga.num_sats;
            snap->hdop        = rx->gga.hdop;
            snap->fix_quality = gnss_fix_quality_from_gga(rx->gga.fix_quality);
            snap->rtk_status  = gnss_rtk_status_from_gga(rx->gga.fix_quality);
            snap->correction_age_valid = rx->gga.age_diff_valid;
            snap->correction_age_s     = rx->gga.age_diff_valid ? rx->gga.age_diff : 0.0;
            snap->last_valid_gga_time_ms = timestamp_ms;
            snap->position_valid = true;
            snap->status_reason = GNSS_REASON_NONE;
        }
        /* BAD GGA (fix=0): update timestamp only, do NOT invalidate snapshot.
         * Freshness timeout will eventually invalidate if no good GGA arrives. */

        rx->gga_dirty = false;
    }

    /* ---- Update motion from RMC ---- */
    if (rx->rmc_dirty && rx->rmc_valid) {
        snap->last_rmc_time_ms = timestamp_ms;

        if (rx->rmc.status_valid) {
            /* GOOD RMC (status A): update motion data AND set valid */
            snap->speed_ms   = gnss_knots_to_ms(rx->rmc.speed_knots);
            snap->course_deg = rx->rmc.course_true;
            snap->last_valid_rmc_time_ms = timestamp_ms;
            snap->motion_valid = true;
            if (snap->status_reason == GNSS_REASON_NONE ||
                snap->status_reason == GNSS_REASON_RMC_VOID ||
                snap->status_reason == GNSS_REASON_NO_RMC) {
                snap->status_reason = GNSS_REASON_NONE;
            }
        }
        /* BAD RMC (status V): update timestamp only, do NOT invalidate snapshot.
         * Freshness timeout will eventually invalidate if no good RMC arrives. */

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

    /* Publish to position snapshot buffer for downstream consumers */
    snapshot_buffer_set(&rx->position_snapshot, snap);
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

    /* Initialize position snapshot buffer for downstream consumers */
    gnss_snapshot_init(&rx->position_storage);
    snapshot_buffer_init(&rx->position_snapshot,
                        &rx->position_storage,
                        sizeof(gnss_snapshot_t));

    /* Register service step callback */
    rx->component.service_step = gnss_um980_service_step;

    /* NAV-FIX-001 AP-B: Register fast path hooks for 100 Hz on Core 1 */
    rx->component.fast_input   = gnss_um980_fast_input;
    rx->component.fast_process = gnss_um980_fast_process;
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
                rx->unknown_prefix_count++;
                break;
            }
            /* R2: Track last valid sentence type and timestamp */
            rx->last_sentence_type = (uint32_t)rx->nmea_parser.type;
            rx->last_sentence_us = (uint64_t)esp_timer_get_time();
        } else if (result == NMEA_RESULT_INVALID_CHECKSUM) {
            rx->checksum_errors++;
            rx->snapshot.last_error = GNSS_ERR_CHECKSUM;
            /* CRITICAL: Invalid checksum → no data copied, no dirty flags set.
             * Existing snapshot fields remain unchanged. */
        } else if (result == NMEA_RESULT_OVERFLOW) {
            rx->overflow_errors++;
            rx->snapshot.last_error = GNSS_ERR_OVERFLOW;
        } else if (result == NMEA_RESULT_BINARY_REJECT) {
            /* NAV-GNSS-NMEA-CORRUPTION-001 WP-E:
             * Binary byte detected within sentence data.
             * Parser has already reset to IDLE for resync. */
            rx->binary_rejects++;
        }
    }

    rx->sentences_parsed += sentences_parsed;

    /* Propagate parser diagnostic counters to GNSS instance */
    rx->garbage_discarded = rx->nmea_parser.garbage_discarded;

    return sentences_parsed;
}

void gnss_um980_finalize_snapshot(gnss_um980_t* rx, uint64_t timestamp_ms)
{
    if (rx == NULL) {
        return;
    }
    gnss_um980_rebuild_snapshot(rx, timestamp_ms);
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
       tmp must be large enough for multiple NMEA sentences (max ~82 bytes each).
       In production, service_step is called repeatedly in a task loop, but tests
       may batch multiple sentences before calling service_step once. */
    uint8_t tmp[256];
    size_t available = byte_ring_buffer_available(rx->rx_source);

    if (available > 0) {
        size_t to_read = available > sizeof(tmp) ? sizeof(tmp) : available;
        size_t pulled = byte_ring_buffer_read(rx->rx_source, tmp, to_read);
        if (pulled > 0) {
            gnss_um980_feed(rx, tmp, pulled);
        }
    }

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

const snapshot_buffer_t* gnss_um980_get_position_snapshot(const gnss_um980_t* rx)
{
    if (rx == NULL) {
        return NULL;
    }
    return &rx->position_snapshot;
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

/* ---- NAV-FIX-001 AP-B: Fast Path Hooks (Core 1, 100 Hz) ---- */

void gnss_um980_fast_input(runtime_component_t* comp, const fast_cycle_context_t* ctx)
{
    gnss_um980_t* rx = (gnss_um980_t*)comp;
    if (rx == NULL || rx->rx_source == NULL) {
        return;
    }

    (void)ctx;  /* timestamp available via ctx->timestamp_us if needed */

    /* Consume bytes from RX source buffer (same logic as service_step) */
    uint8_t tmp[64];
    size_t available = byte_ring_buffer_available(rx->rx_source);

    if (available > 0) {
        size_t to_read = available > sizeof(tmp) ? sizeof(tmp) : available;
        size_t pulled = byte_ring_buffer_read(rx->rx_source, tmp, to_read);
        if (pulled > 0) {
            gnss_um980_feed(rx, tmp, pulled);
        }
    }
}

void gnss_um980_fast_process(runtime_component_t* comp, const fast_cycle_context_t* ctx)
{
    gnss_um980_t* rx = (gnss_um980_t*)comp;
    if (rx == NULL) {
        return;
    }

    uint64_t ts_ms = ctx->timestamp_us / 1000;
    rx->last_service_ms = ts_ms;

    /* Rebuild snapshot if any sentence type was updated by fast_input */
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
}
