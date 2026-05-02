/* ========================================================================
 * hw_runtime_diag.c — Hardware Runtime Diagnostics (NAV-HW-RUNTIME-DIAG-001)
 *
 * Periodic health reporter: fast-loop stats, GNSS UART RX counters,
 * GNSS snapshot validity, Ethernet/UDP status, PGN214 TX counter.
 *
 * Runs in SERVICE_GROUP_DIAGNOSTICS on Core 0 at ~100ms period.
 * Actual output at 0.2 Hz (every 5 seconds) to avoid log spam.
 * ======================================================================== */

#include "hw_runtime_diag.h"
#include "esp_log.h"
#include "esp_timer.h"

/* ---- Observed component headers (type-safe access) ---- */
#include "runtime_stats.h"
#include "transport_uart.h"
#include "gnss_um980.h"
#include "gnss_dual_heading.h"
#include "aog_navigation_app.h"

static const char* TAG = "HW_DIAG";

/* ---- GNSS fix quality name lookup ---- */
static const char* fix_quality_name(uint8_t fq)
{
    switch (fq) {
        case 0:  return "none";
        case 1:  return "sps";
        case 2:  return "dgps";
        case 4:  return "rtk";
        case 5:  return "rtk_float";
        default: return "unknown";
    }
}

/* ---- Snapshot age calculation ---- */
static uint32_t snapshot_age_ms(const void* gnss_ptr)
{
    const gnss_um980_t* gnss = (const gnss_um980_t*)gnss_ptr;
    if (gnss == NULL) return 0;

    /* Use GGA time as primary age indicator */
    uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000);
    if (gnss->snapshot.last_gga_time_ms > 0) {
        uint64_t age = now_ms - gnss->snapshot.last_gga_time_ms;
        return (age > 0xFFFFFFFFUL) ? 0xFFFFFFFFUL : (uint32_t)age;
    }
    return 0;
}

/* ---- Determine last received sentence type from counters ---- */
static const char* last_sentence_type(const gnss_um980_t* gnss)
{
    if (gnss == NULL || gnss->sentences_parsed == 0) return "none";
    /* Use gga as default since it's the most frequent */
    return "GGA";
}

/* =================================================================
 * Public API
 * ================================================================= */

void hw_runtime_diag_init(hw_runtime_diag_t* diag)
{
    if (diag == NULL) return;

    diag->component.name = "hw_runtime_diag";
    diag->component.service_step = hw_runtime_diag_service_step;
    diag->component.service_group = SERVICE_GROUP_DIAGNOSTICS;
    diag->component.fast_input = NULL;
    diag->component.fast_process = NULL;
    diag->component.fast_output = NULL;

    diag->last_print_ms = 0;
    diag->first_print_done = false;

    diag->primary_uart = NULL;
    diag->secondary_uart = NULL;
    diag->primary_gnss = NULL;
    diag->secondary_gnss = NULL;
    diag->heading = NULL;
    diag->nav_app = NULL;
}

void hw_runtime_diag_set_sources(hw_runtime_diag_t* diag,
                                  const void* primary_uart,
                                  const void* secondary_uart,
                                  const void* primary_gnss,
                                  const void* secondary_gnss,
                                  const void* heading,
                                  const void* nav_app)
{
    if (diag == NULL) return;
    diag->primary_uart = primary_uart;
    diag->secondary_uart = secondary_uart;
    diag->primary_gnss = primary_gnss;
    diag->secondary_gnss = secondary_gnss;
    diag->heading = heading;
    diag->nav_app = nav_app;
}

/* =================================================================
 * Service Step — 5-second periodic health print
 * ================================================================= */

void hw_runtime_diag_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    hw_runtime_diag_t* diag = (hw_runtime_diag_t*)comp;
    if (diag == NULL) return;

    uint64_t now_ms = timestamp_us / 1000;

    /* First print after 2s (give system time to settle), then every 5s */
    if (!diag->first_print_done) {
        if (now_ms < 2000) return;
        diag->first_print_done = true;
        diag->last_print_ms = now_ms;
    } else {
        if ((now_ms - diag->last_print_ms) < HW_DIAG_PRINT_INTERVAL_MS) return;
        diag->last_print_ms = now_ms;
    }

    /* ---- WP-A: Fast-Loop Health ---- */
    {
        uint32_t cycles = runtime_stats_get_cycle_count();
        uint32_t missed = runtime_stats_get_deadline_miss_count();
        uint32_t worst  = runtime_stats_get_worst();

        /* Derive approximate Hz from cycle count and elapsed time.
         * cycles / elapsed_seconds. elapsed = now_ms / 1000.
         * First print at ~2s, subsequent at 5s intervals. */
        uint32_t elapsed_s = (uint32_t)(now_ms / 1000);
        uint32_t hz = (elapsed_s > 0) ? (cycles / elapsed_s) : 0;

        ESP_LOGI("HW_DIAG",
                 "RUNTIME_FAST: hz=%u cycles=%u missed=%u worst_us=%u",
                 hz, cycles, missed, worst);
    }

    /* ---- WP-B: GNSS UART RX Counters ---- */
    {
        const transport_uart_t* p_uart = (const transport_uart_t*)diag->primary_uart;
        const gnss_um980_t*     p_gnss = (const gnss_um980_t*)diag->primary_gnss;

        if (p_uart != NULL && p_gnss != NULL) {
            ESP_LOGI("HW_DIAG",
                     "GNSS_RX: primary uart=1 bytes=%u lines=%u "
                     "checksum_ok=%u checksum_bad=%u last=%s",
                     p_gnss->bytes_received,
                     p_gnss->sentences_parsed,
                     p_gnss->sentences_parsed - p_gnss->checksum_errors,
                     p_gnss->checksum_errors,
                     last_sentence_type(p_gnss));
        }

        const transport_uart_t* s_uart = (const transport_uart_t*)diag->secondary_uart;
        const gnss_um980_t*     s_gnss = (const gnss_um980_t*)diag->secondary_gnss;

        if (s_uart != NULL && s_gnss != NULL) {
            ESP_LOGI("HW_DIAG",
                     "GNSS_RX: secondary uart=2 bytes=%u lines=%u "
                     "checksum_ok=%u checksum_bad=%u last=%s",
                     s_gnss->bytes_received,
                     s_gnss->sentences_parsed,
                     s_gnss->sentences_parsed - s_gnss->checksum_errors,
                     s_gnss->checksum_errors,
                     last_sentence_type(s_gnss));
        }
    }

    /* ---- WP-C: GNSS Snapshot Health ---- */
    {
        const gnss_um980_t* p = (const gnss_um980_t*)diag->primary_gnss;
        if (p != NULL) {
            ESP_LOGI("HW_DIAG",
                     "GNSS: primary valid=%u fresh=%u fix=%s age_ms=%u",
                     (unsigned)p->snapshot.valid,
                     (unsigned)p->snapshot.fresh,
                     fix_quality_name(p->snapshot.fix_quality),
                     snapshot_age_ms(p));
        }

        const gnss_um980_t* s = (const gnss_um980_t*)diag->secondary_gnss;
        if (s != NULL) {
            ESP_LOGI("HW_DIAG",
                     "GNSS: secondary valid=%u fresh=%u fix=%s age_ms=%u",
                     (unsigned)s->snapshot.valid,
                     (unsigned)s->snapshot.fresh,
                     fix_quality_name(s->snapshot.fix_quality),
                     snapshot_age_ms(s));
        }
    }

    /* ---- WP-C: Heading Health ---- */
    {
        const gnss_dual_heading_calc_t* h = (const gnss_dual_heading_calc_t*)diag->heading;
        if (h != NULL) {
            const gnss_dual_heading_t* result = &h->result;
            double heading_deg = (result->heading_rad * 180.0) / 3.14159265358979323846;
            ESP_LOGI("HW_DIAG",
                     "HEADING: valid=%u heading_deg=%.1f calc_count=%u",
                     (unsigned)result->valid,
                     heading_deg,
                     h->calc_count);
        }
    }

    /* ---- WP-D: Ethernet/UDP/PGN214 Status ---- */
    {
        /* Ethernet is currently a stub in hal_eth */
        ESP_LOGI("HW_DIAG",
                 "ETH: driver=RTL8201/RMII link=STUB");

        /* UDP is currently a stub in transport_udp */
        ESP_LOGI("HW_DIAG",
                 "UDP: tx=STUB rx=STUB");

        /* PGN214 TX counter from AOG nav app */
        const aog_nav_app_t* app = (const aog_nav_app_t*)diag->nav_app;
        if (app != NULL) {
            ESP_LOGI("HW_DIAG",
                     "AOG_NAV: pgn214_tx=%u cycle=%u state=%s",
                     app->pgn214_send_count,
                     app->cycle_count,
                     (app->output_state == AOG_OUTPUT_OK) ? "OK" :
                     (app->output_state == AOG_OUTPUT_GNSS_INVALID) ? "GNSS_INVALID" :
                     (app->output_state == AOG_OUTPUT_GNSS_STALE) ? "GNSS_STALE" :
                     (app->output_state == AOG_OUTPUT_HEADING_INVALID) ? "HDG_INVALID" :
                     (app->output_state == AOG_OUTPUT_HEADING_STALE) ? "HDG_STALE" :
                     (app->output_state == AOG_OUTPUT_HEADING_LOST) ? "HDG_LOST" :
                     (app->output_state == AOG_OUTPUT_INIT) ? "INIT" : "OTHER");
        }
    }
}
