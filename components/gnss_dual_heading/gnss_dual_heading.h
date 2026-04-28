#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "runtime_component.h"
#include "nmea_parser.h"
#include "gnss_um980.h"
#include "snapshot_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Dual GNSS Heading Result ---- */

typedef struct {
    double heading_rad;       /* heading in radians (0 = north, clockwise) */
    bool   valid;             /* true if both receivers have fix */
    uint8_t primary_fix;      /* fix quality of primary receiver */
    uint8_t secondary_fix;    /* fix quality of secondary receiver */
} gnss_dual_heading_t;

/* ---- Dual Heading Calculator ----
 *
 * Computes heading from two GNSS positions.
 * Input: pointers to primary/secondary gnss_um980 instances (NOT owned).
 * Output: heading snapshot for downstream consumers (via snapshot_buffer_t).
 *
 * IMPORTANT: runtime_component_t MUST be the first field for safe casting. */
typedef struct {
    runtime_component_t component;    /* MUST be first */

    gnss_dual_heading_t result;
    snapshot_buffer_t   heading_snapshot;   /* wraps result for safe consumption */
    gnss_dual_heading_t heading_storage;    /* storage backing the snapshot */
    uint32_t calc_count;

    /* Input references (set by caller, NOT owned) */
    gnss_um980_t* primary;
    gnss_um980_t* secondary;
} gnss_dual_heading_calc_t;

/* ---- API ---- */

/* Initialize dual heading calculator. */
void gnss_dual_heading_init(gnss_dual_heading_calc_t* calc);

/* Set input GNSS references (NOT owned). */
void gnss_dual_heading_set_sources(gnss_dual_heading_calc_t* calc,
                                    gnss_um980_t* primary,
                                    gnss_um980_t* secondary);

/* Update heading from the two referenced GNSS positions.
 * Called by the runtime service loop. */
void gnss_dual_heading_service_step(runtime_component_t* comp, uint64_t timestamp_us);

/* Get latest heading result. NULL if calc is NULL. */
const gnss_dual_heading_t* gnss_dual_heading_get(const gnss_dual_heading_calc_t* calc);

/* Get pointer to the heading snapshot buffer (for wiring to consumers). */
const snapshot_buffer_t* gnss_dual_heading_get_snapshot(const gnss_dual_heading_calc_t* calc);

#ifdef __cplusplus
}
#endif
