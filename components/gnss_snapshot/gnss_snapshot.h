#pragma once
/* ========================================================================
 * gnss_snapshot.h — Unified GNSS Snapshot Model (NAV-GNSS-VALID-001)
 *
 * Merges position/quality data from GGA + RMC + GST into a single
 * atomic snapshot for downstream consumers (dual_heading, AOG app,
 * diagnostics).
 *
 * No parsing, no UART I/O — pure data model + freshness logic.
 *
 * VALIDITY MODEL (Variant A — separate flags):
 *   - position_valid: true when fresh GGA with fix_quality > 0 received
 *   - motion_valid:   true when fresh RMC with status A received
 *   - accuracy_valid: true when fresh GST received (optional enrichment)
 *   - valid:          true when position_valid AND motion_valid
 *   - fresh:          true when all required data is within freshness timeout
 *
 * CONSUMER USAGE:
 *   - NAV-HEADING-001: needs position_valid + fresh (heading from 2 antennas)
 *   - NAV-AOG-001:     needs valid + fresh (full navigation solution)
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Fix Quality (derived from GGA fix_quality byte) ---- */
typedef enum {
    GNSS_FIX_NONE = 0,        /* No fix / invalid */
    GNSS_FIX_SINGLE,          /* Autonomous GPS fix (1) */
    GNSS_FIX_DGPS,            /* Differential GPS (2) */
    GNSS_FIX_PPS,             /* PPS fix (3) — rarely used */
    GNSS_FIX_RTK_FIXED,       /* RTK Fixed (4): integer ambiguity resolved */
    GNSS_FIX_RTK_FLOAT,       /* RTK Float (5): ambiguity unresolved */
    GNSS_FIX_UNKNOWN = 255    /* Unknown/unrecognized fix_quality value */
} gnss_fix_quality_t;

/* ---- RTK Status ---- */
typedef enum {
    GNSS_RTK_NONE = 0,        /* No RTK */
    GNSS_RTK_FLOAT,           /* RTK Float solution */
    GNSS_RTK_FIXED            /* RTK Fixed solution */
} gnss_rtk_status_t;

/* ---- Status Reason (why valid/motion_valid is false) ---- */
typedef enum {
    GNSS_REASON_NONE = 0,      /* All OK */
    GNSS_REASON_NO_FIX,        /* GGA fix_quality = 0 */
    GNSS_REASON_RMC_VOID,      /* RMC status = V (void) */
    GNSS_REASON_NO_GGA,        /* No GGA received yet */
    GNSS_REASON_NO_RMC,        /* No RMC received yet */
    GNSS_REASON_STALE_GGA,     /* GGA freshness timeout expired */
    GNSS_REASON_STALE_RMC,     /* RMC freshness timeout expired */
    GNSS_REASON_UNKNOWN_FIX    /* GGA fix_quality unrecognized */
} gnss_status_reason_t;

/* ---- Error Codes (for diagnostics) ---- */
typedef enum {
    GNSS_ERR_NONE = 0,        /* No error */
    GNSS_ERR_CHECKSUM,        /* NMEA checksum mismatch */
    GNSS_ERR_OVERFLOW,        /* Sentence exceeded buffer */
    GNSS_ERR_TIMEOUT          /* Freshness timeout expired */
} gnss_error_code_t;

/* ---- Freshness defaults ---- */
#define GNSS_FRESHNESS_TIMEOUT_MS_DEFAULT  2000   /* 2 seconds */
#define GNSS_FRESHNESS_TIMEOUT_MS_MAX      30000  /* 30 seconds cap */
#define GNSS_FRESHNESS_TIMEOUT_MS_MIN      100    /* 100 ms floor */

/* ---- GNSS Snapshot ----
 *
 * Unified position/quality/accuracy snapshot. Produced by gnss_um980
 * from merged GGA + RMC + GST NMEA data.
 *
 * All fields are set to zero/false by gnss_snapshot_init().
 * Position fields (lat/lon/alt) are in decimal degrees / meters.
 * Speed is converted from knots to m/s internally. */

typedef struct {
    /* ---- Separate Validity Flags (Variant A) ---- */
    bool         position_valid;  /* true: fresh GGA with fix_quality > 0 */
    bool         motion_valid;    /* true: fresh RMC with status A */
    bool         accuracy_valid;  /* true: fresh GST received (optional) */
    bool         valid;           /* true: position_valid AND motion_valid */
    bool         fresh;           /* true: all required data within timeout */
    uint64_t     timestamp_ms;    /* timestamp of last snapshot update (ms) */

    /* ---- Status Reason ---- */
    gnss_status_reason_t status_reason;  /* reason if valid=false or fresh=false */

    /* ---- Position ---- */
    double       latitude;        /* decimal degrees, N positive, S negative */
    double       longitude;       /* decimal degrees, E positive, W negative */
    double       altitude;        /* above geoid (meters) */

    /* ---- Motion ---- */
    double       speed_ms;        /* speed over ground (m/s, converted from knots) */
    double       course_deg;      /* course over ground (degrees true) */

    /* ---- Quality ---- */
    gnss_fix_quality_t fix_quality;
    gnss_rtk_status_t  rtk_status;
    uint8_t     satellites;       /* satellites in use */
    double      hdop;             /* horizontal dilution of precision */

    /* ---- Correction Age ---- */
    bool        correction_age_valid;  /* true if GGA age_diff field was present */
    double      correction_age_s;      /* age of differential correction (seconds) */

    /* ---- Accuracy estimates (from GST, 0.0 if unavailable) ---- */
    double      std_lat;          /* latitude error 1-sigma (meters) */
    double      std_lon;          /* longitude error 1-sigma (meters) */
    double      std_alt;          /* altitude error 1-sigma (meters) */

    /* ---- Per-sentence-type counters (cumulative since init) ---- */
    uint32_t    gga_count;
    uint32_t    rmc_count;
    uint32_t    gst_count;

    /* ---- Cumulative parser statistics ---- */
    uint32_t    sentences_parsed;
    uint32_t    sentences_error;
    uint32_t    bytes_received;

    /* ---- Timestamps of last received sentence type (ms, 0 = never) ---- */
    uint64_t    last_gga_time_ms;
    uint64_t    last_rmc_time_ms;
    uint64_t    last_gst_time_ms;

    /* ---- Timestamps of last VALID sentence (fix>0 / status A, 0 = never) ---- */
    uint64_t    last_valid_gga_time_ms;
    uint64_t    last_valid_rmc_time_ms;

    /* ---- Last error code for diagnostics ---- */
    gnss_error_code_t last_error;
} gnss_snapshot_t;

/* ---- Snapshot API ---- */

/* Initialize snapshot to zero/invalid state.
 * Must be called before first use. */
void gnss_snapshot_init(gnss_snapshot_t* snap);

/* Check and update freshness based on current time and timeout.
 * Updates snap->fresh, snap->position_valid, snap->motion_valid
 * and snap->status_reason in place.
 * timeout_ms: 0 = use GNSS_FRESHNESS_TIMEOUT_MS_DEFAULT */
void gnss_snapshot_check_freshness(gnss_snapshot_t* snap,
                                   uint64_t current_ms,
                                   uint32_t timeout_ms);

/* Get age of snapshot in milliseconds since last GGA.
 * Returns UINT64_MAX if no GGA has ever been received. */
uint64_t gnss_snapshot_age_ms(const gnss_snapshot_t* snap, uint64_t current_ms);

/* Derive fix_quality from raw NMEA GGA fix_quality byte:
 *   0 → NONE, 1 → SINGLE, 2 → DGPS, 3 → PPS,
 *   4 → RTK_FIXED, 5 → RTK_FLOAT, else → UNKNOWN */
gnss_fix_quality_t gnss_fix_quality_from_gga(uint8_t gga_fix);

/* Derive rtk_status from raw NMEA GGA fix_quality byte:
 *   4 → RTK_FIXED, 5 → RTK_FLOAT, else → NONE */
gnss_rtk_status_t gnss_rtk_status_from_gga(uint8_t gga_fix);

/* Convert speed from knots to meters per second.
 * 1 knot = 0.514444 m/s. */
double gnss_knots_to_ms(double knots);

#ifdef __cplusplus
}
#endif
