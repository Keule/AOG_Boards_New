/* ========================================================================
 * gnss_snapshot.c — GNSS Snapshot Implementation (NAV-GNSS-VALID-001)
 * ======================================================================== */

#include "gnss_snapshot.h"
#include <string.h>
#include <stdint.h>

void gnss_snapshot_init(gnss_snapshot_t* snap)
{
    if (snap == NULL) {
        return;
    }
    memset(snap, 0, sizeof(gnss_snapshot_t));
    snap->position_valid = false;
    snap->motion_valid = false;
    snap->accuracy_valid = false;
    snap->valid = false;
    snap->fresh = false;
    snap->fix_quality = GNSS_FIX_NONE;
    snap->rtk_status = GNSS_RTK_NONE;
    snap->status_reason = GNSS_REASON_NONE;
    snap->last_error = GNSS_ERR_NONE;
    snap->correction_age_valid = false;
}

void gnss_snapshot_check_freshness(gnss_snapshot_t* snap,
                                   uint64_t current_ms,
                                   uint32_t timeout_ms)
{
    if (snap == NULL) return;
    if (timeout_ms == 0) timeout_ms = GNSS_FRESHNESS_TIMEOUT_MS_DEFAULT;

    /* ---- GGA freshness: use last VALID GGA (fix>0) for staleness ---- */
    if (snap->last_valid_gga_time_ms == 0) {
        /* No valid GGA ever received */
        if (snap->position_valid) {
            snap->position_valid = false;
            if (snap->status_reason == GNSS_REASON_NONE) {
                snap->status_reason = GNSS_REASON_NO_GGA;
            }
        }
    } else {
        uint64_t gga_age = (current_ms > snap->last_valid_gga_time_ms)
                           ? current_ms - snap->last_valid_gga_time_ms : 0;
        if (gga_age > timeout_ms) {
            snap->position_valid = false;
            snap->status_reason = GNSS_REASON_STALE_GGA;
        }
    }

    /* ---- RMC freshness: use last VALID RMC (status A) for staleness ---- */
    if (snap->last_valid_rmc_time_ms == 0) {
        if (snap->motion_valid) {
            snap->motion_valid = false;
            if (snap->status_reason == GNSS_REASON_NONE) {
                snap->status_reason = GNSS_REASON_NO_RMC;
            }
        }
    } else {
        uint64_t rmc_age = (current_ms > snap->last_valid_rmc_time_ms)
                           ? current_ms - snap->last_valid_rmc_time_ms : 0;
        if (rmc_age > timeout_ms) {
            snap->motion_valid = false;
            if (snap->status_reason != GNSS_REASON_STALE_GGA) {
                snap->status_reason = GNSS_REASON_STALE_RMC;
            }
        }
    }

    /* ---- GST freshness ---- */
    if (snap->last_gst_time_ms > 0) {
        uint64_t gst_age = (current_ms > snap->last_gst_time_ms)
                           ? current_ms - snap->last_gst_time_ms : 0;
        if (gst_age > timeout_ms) {
            snap->accuracy_valid = false;
        }
    }

    /* ---- Composite valid ---- */
    snap->valid = snap->position_valid && snap->motion_valid;

    /* ---- Fresh ---- */
    if (!snap->valid) {
        snap->fresh = false;
    } else {
        snap->fresh = true;
        snap->last_error = GNSS_ERR_NONE;
        if (snap->status_reason == GNSS_REASON_STALE_GGA ||
            snap->status_reason == GNSS_REASON_STALE_RMC) {
            snap->status_reason = GNSS_REASON_NONE;
        }
    }
}

uint64_t gnss_snapshot_age_ms(const gnss_snapshot_t* snap, uint64_t current_ms)
{
    if (snap == NULL || snap->last_gga_time_ms == 0) {
        return UINT64_MAX;
    }

    /* Clock wrap protection */
    if (current_ms < snap->last_gga_time_ms) {
        return 0;
    }

    return current_ms - snap->last_gga_time_ms;
}

gnss_fix_quality_t gnss_fix_quality_from_gga(uint8_t gga_fix)
{
    switch (gga_fix) {
    case 0: return GNSS_FIX_NONE;
    case 1: return GNSS_FIX_SINGLE;
    case 2: return GNSS_FIX_DGPS;
    case 3: return GNSS_FIX_PPS;
    case 4: return GNSS_FIX_RTK_FIXED;
    case 5: return GNSS_FIX_RTK_FLOAT;
    default: return GNSS_FIX_UNKNOWN;
    }
}

gnss_rtk_status_t gnss_rtk_status_from_gga(uint8_t gga_fix)
{
    switch (gga_fix) {
    case 4: return GNSS_RTK_FIXED;
    case 5: return GNSS_RTK_FLOAT;
    default: return GNSS_RTK_NONE;
    }
}

double gnss_knots_to_ms(double knots)
{
    /* 1 international knot = 0.514444 m/s */
    return knots * 0.514444;
}
