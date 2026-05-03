/* ========================================================================
 * runtime_stats.c — Fast-Loop Cycle Statistics (NAV-ETH-BRINGUP-001-R2 WP-E)
 *
 * Tracks per-cycle processing duration, total cycles, start-to-start
 * period jitter, deadline misses, and computes actual working frequency
 * over a real time window for diagnostic output.
 *
 * All counters are updated in task_fast (Core 1, pinned).
 * All getters are read in hw_runtime_diag (Core 0, DIAG service group).
 * No locking needed — 32-bit aligned reads on ESP32 (Xtensa) are atomic.
 * 64-bit total_cycles is written once per cycle; concurrent readers may
 * see a stale value by at most one cycle, which is acceptable for
 * diagnostic display.
 * ======================================================================== */

#include "runtime_stats.h"
#include "esp_timer.h"

#define RUNTIME_STATS_WINDOW_SIZE  1000
#define FAST_PERIOD_TARGET_US      10000  /* 10 ms = 100 Hz target */

/* ---- Legacy window for worst-case tracking ---- */
static uint32_t s_window[RUNTIME_STATS_WINDOW_SIZE];
static uint32_t s_window_index = 0;
static uint32_t s_window_fill  = 0;
static uint32_t s_last_duration_us = 0;
static uint32_t s_worst_duration_us = 0;

/* ---- NAV-ETH-BRINGUP-001-R2 WP-E: Precise counters ---- */
static uint64_t s_total_cycles = 0;           /* never resets */

/* Deadline miss tracking */
static uint32_t s_deadline_miss_count = 0;

/* Per-cycle timing */
static uint32_t s_processing_us = 0;          /* current cycle duration */

/* Period tracking (start-to-start) */
static int64_t  s_prev_cycle_start_us = 0;    /* for period measurement */
static uint32_t s_period_sum_us = 0;          /* sum of all periods in report window */
static uint32_t s_period_max_us = 0;          /* max period in report window */
static uint32_t s_period_count = 0;           /* number of periods measured in report window */

/* Report window for Hz calculation */
static int64_t  s_report_start_us = 0;        /* timestamp when report window was opened */
static uint64_t s_report_start_cycles = 0;    /* total_cycles when report window was opened */
static uint32_t s_report_cycles = 0;          /* cycles counted in this report window */

/* ---- Init ---- */

void runtime_stats_init(void)
{
    uint32_t i;
    for (i = 0; i < RUNTIME_STATS_WINDOW_SIZE; i++) {
        s_window[i] = 0;
    }
    s_window_index = 0;
    s_window_fill  = 0;
    s_last_duration_us = 0;
    s_worst_duration_us = 0;
    s_total_cycles = 0;
    s_deadline_miss_count = 0;
    s_processing_us = 0;
    s_prev_cycle_start_us = 0;
    s_period_sum_us = 0;
    s_period_max_us = 0;
    s_period_count = 0;
    s_report_start_us = 0;
    s_report_start_cycles = 0;
    s_report_cycles = 0;
}

/* ---- Record cycle start (call BEFORE fast hooks) ---- */

void runtime_stats_record_cycle_start(int64_t start_us)
{
    /* ---- Period measurement (start-to-start) ---- */
    if (s_prev_cycle_start_us > 0) {
        int64_t delta = start_us - s_prev_cycle_start_us;
        if (delta > 0) {
            uint32_t period_us = (uint32_t)delta;
            s_period_sum_us += period_us;
            if (period_us > s_period_max_us) {
                s_period_max_us = period_us;
            }
            s_period_count++;
        }
    }
    s_prev_cycle_start_us = start_us;
}

/* ---- Record cycle end (call AFTER fast hooks) ---- */

void runtime_stats_record(uint32_t cycle_duration_us)
{
    /* Update legacy window */
    s_window[s_window_index] = cycle_duration_us;
    s_window_index = (s_window_index + 1U) % RUNTIME_STATS_WINDOW_SIZE;
    if (s_window_fill < RUNTIME_STATS_WINDOW_SIZE) {
        s_window_fill++;
    }
    s_last_duration_us = cycle_duration_us;
    s_processing_us = cycle_duration_us;

    /* Update worst */
    if (cycle_duration_us > s_worst_duration_us) {
        s_worst_duration_us = cycle_duration_us;
    }

    /* Increment counters */
    s_total_cycles++;
    s_report_cycles++;
}

/* ---- Deadline miss ---- */

void runtime_stats_record_deadline_miss(void)
{
    s_deadline_miss_count++;
}

/* ---- Getters: Legacy ---- */

uint32_t runtime_stats_get_last(void)
{
    return s_last_duration_us;
}

uint32_t runtime_stats_get_worst(void)
{
    return s_worst_duration_us;
}

uint32_t runtime_stats_get_cycle_count(void)
{
    return s_window_fill;  /* legacy: window fill, NOT total */
}

/* ---- Getters: Total cycles ---- */

uint64_t runtime_stats_get_total_cycles(void)
{
    return s_total_cycles;
}

/* ---- Getters: Deadline miss ---- */

uint32_t runtime_stats_get_deadline_miss_count(void)
{
    return s_deadline_miss_count;
}

/* ---- Getters: Precise diagnostics ---- */

uint32_t runtime_stats_get_processing_us(void)
{
    return s_processing_us;
}

uint32_t runtime_stats_get_remaining_us(void)
{
    uint32_t remaining = FAST_PERIOD_TARGET_US - s_processing_us;
    return (s_processing_us < FAST_PERIOD_TARGET_US) ? remaining : 0;
}

uint32_t runtime_stats_get_hz_centi(void)
{
    if (s_report_cycles == 0) return 0;

    /* Use s_total_cycles and s_report_start_cycles for accurate count.
     * But s_report_cycles should already be correct. */
    uint64_t elapsed_cycles = s_total_cycles - s_report_start_cycles;
    if (elapsed_cycles == 0) return 0;

    /* Get current time (called from diagnostic context on Core 0) */
    int64_t now_us = esp_timer_get_time();

    int64_t elapsed_us = now_us - s_report_start_us;
    if (elapsed_us <= 0) return 0;

    /* hz_centi = cycles * 1000000 / elapsed_us * 100 (centi-Hz)
     * To avoid 64-bit division complexity, use:
     * hz_centi = (cycles * 100000000) / elapsed_us
     * Max cycles in 5s window at 100Hz = 500. 500 * 100000000 = 5e10, fits in uint64. */
    uint64_t hz_centi = (elapsed_cycles * 100000000ULL) / (uint64_t)elapsed_us;

    /* Cap at 1000 Hz (100000 centi-Hz) — sanity limit */
    if (hz_centi > 100000) hz_centi = 100000;

    return (uint32_t)hz_centi;
}

uint32_t runtime_stats_get_report_cycles(void)
{
    return s_report_cycles;
}

void runtime_stats_reset_report_window(int64_t now_us)
{
    s_report_start_us = now_us;
    s_report_start_cycles = s_total_cycles;
    s_report_cycles = 0;

    /* Reset period tracking for next window */
    s_period_sum_us = 0;
    s_period_max_us = 0;
    s_period_count = 0;
}

uint32_t runtime_stats_get_period_avg_us(void)
{
    if (s_period_count == 0) return 0;
    return s_period_sum_us / s_period_count;
}

uint32_t runtime_stats_get_period_max_us(void)
{
    return s_period_max_us;
}
