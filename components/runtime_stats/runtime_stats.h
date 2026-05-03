#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Basic cycle stats (legacy, kept for compatibility) ---- */

void runtime_stats_init(void);
void runtime_stats_record(uint32_t cycle_duration_us);
uint32_t runtime_stats_get_last(void);
uint32_t runtime_stats_get_worst(void);

/* NAV-ETH-BRINGUP-001-R2 WP-E: Total cycle count (never resets within session).
 * Monotonically increasing. Overflows after ~4.29 billion cycles (~497 days at 100 Hz). */
uint64_t runtime_stats_get_total_cycles(void);

/* ---- Deadline miss tracking (NAV-FIX-001 AP-C) ---- */

void runtime_stats_record_deadline_miss(void);
uint32_t runtime_stats_get_deadline_miss_count(void);

/* ---- NAV-ETH-BRINGUP-001-R2 WP-E: Precise fast-loop diagnostics ---- */

/* Record cycle start timestamp (call BEFORE fast hooks).
 * Used for period measurement (start-to-start interval). */
void runtime_stats_record_cycle_start(int64_t start_us);

/* Get current cycle processing time in microseconds.
 * Returns the duration of the most recent cycle. */
uint32_t runtime_stats_get_processing_us(void);

/* Get remaining budget in microseconds: max(0, 10000 - processing_us). */
uint32_t runtime_stats_get_remaining_us(void);

/* Get actual frequency in centi-Hz (i.e., 10000 = 100.00 Hz).
 * Computed over a real time window from the last report reset.
 * Returns 0 if no cycles recorded or window is empty. */
uint32_t runtime_stats_get_hz_centi(void);

/* Get number of cycles since last report reset. */
uint32_t runtime_stats_get_report_cycles(void);

/* Reset report window (call at start of each diagnostic print).
 * Snapshots the current total_cycles and timestamp for next hz calculation. */
void runtime_stats_reset_report_window(int64_t now_us);

/* Get average start-to-start period in microseconds over the report window.
 * Returns 0 if fewer than 2 cycles recorded. */
uint32_t runtime_stats_get_period_avg_us(void);

/* Get maximum start-to-start period in microseconds over the report window.
 * Returns 0 if fewer than 2 cycles recorded. */
uint32_t runtime_stats_get_period_max_us(void);

/* Legacy alias — returns min(window_fill, 1000).
 * NOTE: This is NOT total cycles. Use runtime_stats_get_total_cycles() for that. */
uint32_t runtime_stats_get_cycle_count(void);

#ifdef __cplusplus
}
#endif
