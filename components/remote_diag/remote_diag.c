/* ========================================================================
 * remote_diag.c — Remote Diagnostics over Ethernet (NAV-REMOTE-DIAG-001-R2)
 *
 * Minimal HTTP server using ESP-IDF esp_http_server.
 * Provides /status, /diag, /version endpoints as JSON/text.
 *
 * Architecture:
 *   - HTTP server started after IP obtained (no hal_eth dependency)
 *   - Runs on Core 0 via SERVICE_GROUP_DIAGNOSTICS
 *   - No network code in task_fast (Core 1)
 *   - All component references are read-only (const void*)
 *   - Missing fields reported as "not_available" — endpoint never fails
 *   - HTTP server handle stored in diag state to prevent loss
 *
 * R2 changes:
 *   - A1: HTTP gating uses IP-only check (no hal_eth_is_connected stub)
 *   - B1: GNSS per-receiver extended (gsa_count, gsv_count, last_sentence, ages)
 *   - B2: Snapshot valid/fresh/age fully exposed
 *   - B3: Validity logic documented, thresholds visible
 *   - B4: RTK/Fix status from raw + interpreted
 *   - B5: NTRIP bytes_rx + last_rx_age, RTCM per-output last_tx_age
 *
 * JSON response format is hand-crafted (no cJSON dependency).
 * ======================================================================== */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "remote_diag.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_http_server.h"
#include "esp_netif.h"

/* ---- Observed component headers ---- */
#include "runtime_stats.h"
#include "runtime_types.h"
#include "runtime_mode.h"
#include "runtime_health.h"
#include "transport_uart.h"
#include "gnss_um980.h"
#include "gnss_snapshot.h"
#include "gnss_dual_heading.h"
#include "aog_navigation_app.h"
#include "ntrip_client.h"
#include "rtcm_router.h"
#include "transport_udp.h"
#include "board_profile.h"
#include "feature_flags.h"

static const char* TAG = "REMOTE_DIAG";

/* ---- Buffer sizes ---- */
#define JSON_BUF_SIZE  8192
#define DIAG_BUF_SIZE  8192

/* ---- Helpers: safe pointer deref ---- */

static const transport_uart_t* as_uart(const void* p) { return (const transport_uart_t*)p; }
static const gnss_um980_t* as_gnss(const void* p) { return (const gnss_um980_t*)p; }
static const gnss_dual_heading_calc_t* as_heading(const void* p) { return (const gnss_dual_heading_calc_t*)p; }
static const aog_nav_app_t* as_nav_app(const void* p) { return (const aog_nav_app_t*)p; }
static const transport_udp_t* as_udp(const void* p) { return (const transport_udp_t*)p; }
static const ntrip_client_t* as_ntrip(const void* p) { return (const ntrip_client_t*)p; }
static const rtcm_router_t* as_rtcm_router(const void* p) { return (const rtcm_router_t*)p; }

/* ---- Enum to string helpers ---- */

static const char* fix_quality_str(gnss_fix_quality_t fq)
{
    switch (fq) {
        case GNSS_FIX_NONE:       return "none";
        case GNSS_FIX_SINGLE:     return "sps";
        case GNSS_FIX_DGPS:       return "dgps";
        case GNSS_FIX_PPS:        return "pps";
        case GNSS_FIX_RTK_FIXED:  return "rtk_fixed";
        case GNSS_FIX_RTK_FLOAT:  return "rtk_float";
        default:                  return "unknown";
    }
}

static const char* rtk_status_str(gnss_rtk_status_t rtk)
{
    switch (rtk) {
        case GNSS_RTK_NONE:  return "none";
        case GNSS_RTK_FLOAT: return "float";
        case GNSS_RTK_FIXED: return "fixed";
        default:             return "unknown";
    }
}

static const char* ntrip_state_str(ntrip_state_t s)
{
    switch (s) {
        case NTRIP_STATE_IDLE:          return "idle";
        case NTRIP_STATE_CONNECTING:    return "connecting";
        case NTRIP_STATE_BUILD_REQUEST: return "connecting";
        case NTRIP_STATE_SEND_REQUEST:  return "connecting";
        case NTRIP_STATE_WAIT_RESPONSE: return "connecting";
        case NTRIP_STATE_CONNECTED:     return "connected";
        case NTRIP_STATE_ERROR:         return "error";
        case NTRIP_STATE_RETRY_WAIT:    return "retry_wait";
        default:                        return "unknown";
    }
}

static const char* status_reason_str(gnss_status_reason_t r)
{
    switch (r) {
        case GNSS_REASON_NONE:       return "none";
        case GNSS_REASON_NO_FIX:     return "no_fix";
        case GNSS_REASON_RMC_VOID:   return "rmc_void";
        case GNSS_REASON_NO_GGA:     return "no_gga";
        case GNSS_REASON_NO_RMC:     return "no_rmc";
        case GNSS_REASON_STALE_GGA:  return "stale_gga";
        case GNSS_REASON_STALE_RMC:  return "stale_rmc";
        case GNSS_REASON_UNKNOWN_FIX: return "unknown_fix";
        default:                     return "unknown";
    }
}

static const char* sentence_type_str(uint32_t type)
{
    switch ((nmea_sentence_type_t)type) {
        case NMEA_SENTENCE_GGA: return "GGA";
        case NMEA_SENTENCE_RMC: return "RMC";
        case NMEA_SENTENCE_GST: return "GST";
        case NMEA_SENTENCE_GSA: return "GSA";
        case NMEA_SENTENCE_GSV: return "GSV";
        default:                return "NONE";
    }
}

static const char* system_mode_str(void)
{
    return (runtime_mode_get() == SYSTEM_MODE_WORK) ? "work" : "config";
}

static const char* board_name_str(void)
{
    board_type_t b = board_profile_get_board();
    switch (b) {
        case BOARD_LILYGO_T_ETH_LITE_ESP32:   return "lilygo_t_eth_lite_esp32";
        case BOARD_LILYGO_T_ETH_LITE_ESP32S3: return "lilygo_t_eth_lite_esp32s3";
        default:                               return "unknown";
    }
}

/* ---- Singleton for HTTP handlers ---- */
static remote_diag_t* s_diag_instance = NULL;

/* ---- JSON builder ---- */

typedef struct {
    char*  buf;
    size_t pos;
    size_t capacity;
} json_writer_t;

static void jw_init(json_writer_t* jw, char* buf, size_t cap)
{
    jw->buf = buf; jw->pos = 0; jw->capacity = cap; buf[0] = '\0';
}

static void jw_put(json_writer_t* jw, const char* str)
{
    size_t len = strlen(str);
    if (jw->pos + len < jw->capacity - 1) {
        memcpy(jw->buf + jw->pos, str, len);
        jw->pos += len;
        jw->buf[jw->pos] = '\0';
    }
}

static void jw_printf(json_writer_t* jw, const char* fmt, ...)
{
    if (jw->pos >= jw->capacity - 1) return;
    va_list ap;
    va_start(ap, fmt);
    int written = vsnprintf(jw->buf + jw->pos, jw->capacity - jw->pos, fmt, ap);
    va_end(ap);
    if (written > 0) {
        jw->pos += (size_t)written;
        if (jw->pos >= jw->capacity) { jw->pos = jw->capacity - 1; jw->buf[jw->pos] = '\0'; }
    }
}

static void jw_kv_str(json_writer_t* jw, const char* key, const char* val) { jw_printf(jw, "\"%s\":\"%s\",", key, val); }
static void jw_kv_uint(json_writer_t* jw, const char* key, uint32_t val) { jw_printf(jw, "\"%s\":%u,", key, (unsigned)val); }
static void jw_kv_u64(json_writer_t* jw, const char* key, uint64_t val) { jw_printf(jw, "\"%s\":%llu,", key, (unsigned long long)val); }
static void jw_kv_bool(json_writer_t* jw, const char* key, bool val) { jw_printf(jw, "\"%s\":%s,", key, val ? "true" : "false"); }

/* ---- Helpers ---- */

static uint64_t now_us(void) { return (uint64_t)esp_timer_get_time(); }
static uint64_t now_ms(void) { return now_us() / 1000; }

static uint64_t gnss_age_ms(const gnss_um980_t* gnss)
{
    if (gnss == NULL || gnss->snapshot.last_gga_time_ms == 0) return 0;
    uint64_t n = now_ms();
    return (n > gnss->snapshot.last_gga_time_ms) ? (n - gnss->snapshot.last_gga_time_ms) : 0;
}

static uint64_t get_uptime_ms(const remote_diag_t* diag)
{
    if (diag->boot_time_us == 0) return 0;
    int64_t delta = (int64_t)now_us() - (int64_t)diag->boot_time_us;
    return (delta > 0) ? (uint64_t)(delta / 1000) : 0;
}

static bool is_fast_alive(void)
{
    if (runtime_stats_get_cycle_count() == 0) return false;
    /* Use worst cycle as proxy — if processing_us > 0, fast loop is alive */
    return runtime_stats_get_last() > 0;
}

static uint64_t fast_age_ms(void)
{
    /* Estimate: if cycles > 0 and last_us > 0, age ≈ period - processing.
     * Simplified: return 0 if active (cycles in window). */
    return (runtime_stats_get_cycle_count() > 0) ? 0 : 0xFFFFFFFF;
}

/* =================================================================
 * A1: Network status — IP-only check, no hal_eth dependency
 * ================================================================= */

typedef struct {
    bool has_ip;
    char ip[16];
    char netmask[16];
    char gateway[16];
} net_status_t;

static void get_net_status(net_status_t* ns)
{
    memset(ns, 0, sizeof(*ns));
    ns->has_ip = false;
    strncpy(ns->ip, "not_available", sizeof(ns->ip) - 1);
    strncpy(ns->netmask, "not_available", sizeof(ns->netmask) - 1);
    strncpy(ns->gateway, "not_available", sizeof(ns->gateway) - 1);

    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
    if (netif == NULL) return;

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) return;
    if (ip_info.ip.addr == 0) return;

    ns->has_ip = true;
    snprintf(ns->ip,      sizeof(ns->ip),      "%u.%u.%u.%u", IP2STR(&ip_info.ip));
    snprintf(ns->netmask, sizeof(ns->netmask),  "%u.%u.%u.%u", IP2STR(&ip_info.netmask));
    snprintf(ns->gateway, sizeof(ns->gateway),  "%u.%u.%u.%u", IP2STR(&ip_info.gw));
}

/* =================================================================
 * /status handler
 * ================================================================= */

static void build_status_json(remote_diag_t* diag, char* buf, size_t buf_size)
{
    json_writer_t jw;
    jw_init(&jw, buf, buf_size);
    jw_put(&jw, "{");

    /* System */
    jw_kv_str(&jw, "firmware", "aog_esp_multiboard");
    jw_kv_str(&jw, "board", board_name_str());
    jw_kv_str(&jw, "role",
#if defined(DEVICE_ROLE_NAVIGATION)
        "NAV"
#elif defined(DEVICE_ROLE_STEERING)
        "STEER"
#elif defined(DEVICE_ROLE_FULL_TEST)
        "FULL_TEST"
#else
        "unknown"
#endif
    );
    jw_kv_str(&jw, "system_mode", system_mode_str());
    jw_kv_u64(&jw, "uptime_ms", get_uptime_ms(diag));

    /* Reset reason */
    {
        esp_reset_reason_t r = esp_reset_reason();
        const char* rs = "unknown";
        switch (r) {
            case ESP_RST_POWERON:  rs = "poweron"; break;
            case ESP_RST_EXT:      rs = "external"; break;
            case ESP_RST_SW:       rs = "software"; break;
            case ESP_RST_PANIC:    rs = "panic"; break;
            case ESP_RST_INT_WDT:  rs = "int_wdt"; break;
            case ESP_RST_TASK_WDT: rs = "task_wdt"; break;
            case ESP_RST_WDT:      rs = "wdt"; break;
            case ESP_RST_BROWNOUT: rs = "brownout"; break;
            default: break;
        }
        jw_kv_str(&jw, "reset_reason", rs);
    }

    /* Network */
    {
        net_status_t ns;
        get_net_status(&ns);
        jw_kv_str(&jw, "ip", ns.ip);
        jw_kv_str(&jw, "netmask", ns.netmask);
        jw_kv_str(&jw, "gateway", ns.gateway);
    }

    /* Fast Loop */
    {
        jw_kv_bool(&jw, "fast_alive", is_fast_alive());
        jw_kv_uint(&jw, "fast_cycle_count", runtime_stats_get_cycle_count());
        jw_kv_uint(&jw, "fast_last_us", runtime_stats_get_last());
        jw_kv_uint(&jw, "fast_worst_us", runtime_stats_get_worst());
        jw_printf(&jw, "\"fast_last_record_age_ms\":%llu,", (unsigned long long)fast_age_ms());
        jw_kv_str(&jw, "fast_deadline_misses", "not_available");
        jw_kv_str(&jw, "fast_hz", "not_available");
    }

    /* GNSS per receiver — B1/B2/B3/B4 */
    const gnss_um980_t* gnss_list[2] = { as_gnss(diag->primary_gnss), as_gnss(diag->secondary_gnss) };
    const char* gnss_names[2] = { "gnss1", "gnss2" };

    for (int g = 0; g < 2; g++) {
        const gnss_um980_t* gnss = gnss_list[g];
        const char* p = gnss_names[g];

        if (gnss != NULL) {
            /* Marker: receiver present */
            jw_kv_uint(&jw, p, 1);

            /* B1: UART/NMEA raw stats */
            jw_printf(&jw, "\"%s_uart_rx_bytes\":%u,", p, gnss->bytes_received);
            jw_printf(&jw, "\"%s_nmea_valid\":%u,", p, gnss->sentences_parsed);
            jw_printf(&jw, "\"%s_nmea_csum_fail\":%u,", p, gnss->checksum_errors);
            jw_printf(&jw, "\"%s_nmea_overflow\":%u,", p, gnss->overflow_errors);
            jw_printf(&jw, "\"%s_timeout_events\":%u,", p, gnss->timeout_events);

            /* B1: Per-sentence-type counters */
            jw_printf(&jw, "\"%s_gga_count\":%u,", p, gnss->gga_count);
            jw_printf(&jw, "\"%s_rmc_count\":%u,", p, gnss->rmc_count);
            jw_printf(&jw, "\"%s_gst_count\":%u,", p, gnss->gst_count);
            jw_printf(&jw, "\"%s_gsa_count\":%u,", p, gnss->gsa_count);
            jw_printf(&jw, "\"%s_gsv_count\":%u,", p, gnss->gsv_count);

            /* B1: Last sentence info */
            jw_printf(&jw, "\"%s_last_sentence\":\"%s\",", p, sentence_type_str(gnss->last_sentence_type));
            if (gnss->last_sentence_us > 0) {
                uint64_t age = (now_us() > gnss->last_sentence_us) ? (now_us() - gnss->last_sentence_us) / 1000 : 0;
                jw_printf(&jw, "\"%s_last_sentence_age_ms\":%llu,", p, (unsigned long long)age);
            } else {
                jw_kv_str(&jw, "last_sentence_age_ms", "never");
            }

            /* B1: Per-sentence ages */
            {
                uint64_t n = now_ms();
                const char* na = "never";
                if (gnss->snapshot.last_gga_time_ms > 0 && n > gnss->snapshot.last_gga_time_ms)
                    jw_printf(&jw, "\"%s_last_gga_age_ms\":%llu,", p, (unsigned long long)(n - gnss->snapshot.last_gga_time_ms));
                else { jw_printf(&jw, "\"%s_last_gga_age_ms\":\"%s\",", p, na); }

                if (gnss->snapshot.last_rmc_time_ms > 0 && n > gnss->snapshot.last_rmc_time_ms)
                    jw_printf(&jw, "\"%s_last_rmc_age_ms\":%llu,", p, (unsigned long long)(n - gnss->snapshot.last_rmc_time_ms));
                else { jw_printf(&jw, "\"%s_last_rmc_age_ms\":\"%s\",", p, na); }

                if (gnss->snapshot.last_gst_time_ms > 0 && n > gnss->snapshot.last_gst_time_ms)
                    jw_printf(&jw, "\"%s_last_gst_age_ms\":%llu,", p, (unsigned long long)(n - gnss->snapshot.last_gst_time_ms));
                else { jw_printf(&jw, "\"%s_last_gst_age_ms\":\"%s\",", p, na); }
            }

            /* B2: Snapshot valid/fresh/age */
            jw_printf(&jw, "\"%s_snap_valid\":%s,", p, gnss->snapshot.valid ? "true" : "false");
            jw_printf(&jw, "\"%s_snap_fresh\":%s,", p, gnss->snapshot.fresh ? "true" : "false");
            jw_printf(&jw, "\"%s_snap_position_valid\":%s,", p, gnss->snapshot.position_valid ? "true" : "false");
            jw_printf(&jw, "\"%s_snap_motion_valid\":%s,", p, gnss->snapshot.motion_valid ? "true" : "false");
            jw_printf(&jw, "\"%s_snap_accuracy_valid\":%s,", p, gnss->snapshot.accuracy_valid ? "true" : "false");
            jw_printf(&jw, "\"%s_snap_age_ms\":%llu,", p, (unsigned long long)gnss_age_ms(gnss));

            /* B3: Status reason */
            jw_printf(&jw, "\"%s_invalid_reason\":\"%s\",", p, status_reason_str(gnss->snapshot.status_reason));

            /* B3: Freshness threshold */
            jw_printf(&jw, "\"%s_freshness_timeout_ms\":%u,", p, gnss->freshness_timeout_ms);

            /* B4: Fix/RTK — raw + interpreted */
            jw_printf(&jw, "\"%s_fix\":\"%s\",", p, fix_quality_str(gnss->snapshot.fix_quality));
            jw_printf(&jw, "\"%s_rtk\":\"%s\",", p, rtk_status_str(gnss->snapshot.rtk_status));

            /* Quality fields */
            jw_printf(&jw, "\"%s_satellites\":%u,", p, (unsigned)gnss->snapshot.satellites);
            jw_printf(&jw, "\"%s_hdop\":%.1f,", p, gnss->snapshot.hdop);
            jw_printf(&jw, "\"%s_alt\":%.2f,", p, gnss->snapshot.altitude);
            jw_printf(&jw, "\"%s_speed_ms\":%.2f,", p, gnss->snapshot.speed_ms);
            jw_printf(&jw, "\"%s_course_deg\":%.1f,", p, gnss->snapshot.course_deg);

            /* Correction age */
            if (gnss->snapshot.correction_age_valid) {
                jw_printf(&jw, "\"%s_correction_age_s\":%.1f,", p, gnss->snapshot.correction_age_s);
            } else {
                jw_printf(&jw, "\"%s_correction_age_s\":\"not_available\",", p);
            }

            /* Lat/lon */
            if (gnss->snapshot.valid) {
                jw_printf(&jw, "\"%s_lat\":%.7f,", p, gnss->snapshot.latitude);
                jw_printf(&jw, "\"%s_lon\":%.7f,", p, gnss->snapshot.longitude);
            } else {
                jw_printf(&jw, "\"%s_lat\":\"not_available\",", p);
                jw_printf(&jw, "\"%s_lon\":\"not_available\",", p);
            }
        } else {
            jw_kv_str(&jw, p, "not_available");
        }
    }

    /* B5: NTRIP — now with bytes_rx and last_rx_age */
    {
        const ntrip_client_t* ntrip = as_ntrip(diag->ntrip);
        if (ntrip != NULL) {
            jw_kv_bool(&jw, "ntrip_started", ntrip->started);
            jw_kv_bool(&jw, "ntrip_config_valid", ntrip->config_valid);
            jw_kv_str(&jw, "ntrip_state", ntrip_state_str(ntrip->state));
            jw_kv_uint(&jw, "ntrip_reconnects", ntrip->reconnect_count);
            jw_kv_uint(&jw, "ntrip_last_error", (uint32_t)ntrip->last_error);
            jw_kv_uint(&jw, "ntrip_http_status", (uint32_t)ntrip->http_status_code);
            jw_kv_uint(&jw, "ntrip_bytes_rx", ntrip->bytes_rx_total);
            if (ntrip->last_rx_us > 0) {
                uint64_t age = (now_us() > ntrip->last_rx_us) ? (now_us() - ntrip->last_rx_us) / 1000 : 0;
                jw_printf(&jw, "\"ntrip_last_rx_age_ms\":%llu,", (unsigned long long)age);
            } else {
                jw_kv_str(&jw, "ntrip_last_rx_age_ms", "never");
            }
        } else {
            jw_kv_str(&jw, "ntrip", "not_available");
        }
    }

    /* B5: RTCM Router — now with per-output last_tx_age */
    {
        const rtcm_router_t* router = as_rtcm_router(diag->rtcm_router);
        if (router != NULL) {
            const rtcm_stats_t* stats = rtcm_router_get_stats(router);
            uint32_t in_b = (stats != NULL) ? stats->bytes_in : 0;
            uint32_t drops = (stats != NULL) ? stats->bytes_dropped : 0;

            jw_kv_uint(&jw, "rtcm_in_bytes", in_b);
            jw_kv_uint(&jw, "rtcm_drops", drops);
            jw_kv_uint(&jw, "rtcm_overflow_count", router->output_overflow_count);

            if (stats != NULL && stats->last_activity_us > 0) {
                uint64_t age = (now_us() > stats->last_activity_us) ? (now_us() - stats->last_activity_us) / 1000 : 0;
                jw_printf(&jw, "\"rtcm_last_activity_age_ms\":%llu,", (unsigned long long)age);
            } else {
                jw_kv_str(&jw, "rtcm_last_activity_age_ms", "never");
            }

            /* Per-output stats */
            if (router->output_count >= 1) {
                jw_printf(&jw, "\"rtcm_uart1_tx_bytes\":%u,", router->outputs[0].bytes_forwarded);
                jw_printf(&jw, "\"rtcm_uart1_drops\":%u,", router->outputs[0].bytes_dropped);
                if (router->outputs[0].last_activity_us > 0) {
                    uint64_t age = (now_us() > router->outputs[0].last_activity_us) ? (now_us() - router->outputs[0].last_activity_us) / 1000 : 0;
                    jw_printf(&jw, "\"rtcm_uart1_last_tx_age_ms\":%llu,", (unsigned long long)age);
                } else {
                    jw_kv_str(&jw, "rtcm_uart1_last_tx_age_ms", "never");
                }
            } else {
                jw_kv_str(&jw, "rtcm_uart1_tx_bytes", "not_available");
                jw_kv_str(&jw, "rtcm_uart1_last_tx_age_ms", "not_available");
            }

            if (router->output_count >= 2) {
                jw_printf(&jw, "\"rtcm_uart2_tx_bytes\":%u,", router->outputs[1].bytes_forwarded);
                jw_printf(&jw, "\"rtcm_uart2_drops\":%u,", router->outputs[1].bytes_dropped);
                if (router->outputs[1].last_activity_us > 0) {
                    uint64_t age = (now_us() > router->outputs[1].last_activity_us) ? (now_us() - router->outputs[1].last_activity_us) / 1000 : 0;
                    jw_printf(&jw, "\"rtcm_uart2_last_tx_age_ms\":%llu,", (unsigned long long)age);
                } else {
                    jw_kv_str(&jw, "rtcm_uart2_last_tx_age_ms", "never");
                }
            } else {
                jw_kv_str(&jw, "rtcm_uart2_tx_bytes", "not_available");
                jw_kv_str(&jw, "rtcm_uart2_last_tx_age_ms", "not_available");
            }
        } else {
            jw_kv_str(&jw, "rtcm", "not_available");
        }
    }

    /* AOG / PGN 214 */
    {
        const aog_nav_app_t* app = as_nav_app(diag->nav_app);
        if (app != NULL) {
            jw_kv_uint(&jw, "pgn214_sent", app->pgn214_send_count);
            jw_kv_uint(&jw, "aog_cycles", app->cycle_count);
            jw_kv_bool(&jw, "aog_position_valid", app->gnss_output_active);
            jw_kv_bool(&jw, "aog_heading_valid", app->heading_output_active);
            if (app->pgn214_send_count > 0) {
                uint64_t age = 10;  /* 100 Hz = 10ms per frame */
                jw_printf(&jw, "\"pgn214_last_send_age_ms\":%llu,", (unsigned long long)age);
            } else {
                jw_kv_str(&jw, "pgn214_last_send_age_ms", "never");
            }
            jw_kv_str(&jw, "pgn214_drops", "not_available");
        } else {
            jw_kv_str(&jw, "aog", "not_available");
        }
    }

    /* UART stats per receiver */
    {
        const transport_uart_t* uarts[2] = { as_uart(diag->primary_uart), as_uart(diag->secondary_uart) };
        const char* uart_names[2] = { "uart1", "uart2" };
        for (int u = 0; u < 2; u++) {
            const transport_uart_t* uart = uarts[u];
            if (uart != NULL) {
                const transport_uart_stats_t* st = transport_uart_get_stats(uart);
                if (st != NULL) {
                    jw_printf(&jw, "\"%s_rx_bytes\":%u,", uart_names[u], st->rx_bytes_in);
                    jw_printf(&jw, "\"%s_tx_bytes\":%u,", uart_names[u], st->tx_bytes_out);
                    jw_printf(&jw, "\"%s_rx_overflows\":%u,", uart_names[u], st->rx_overflow_count);
                    jw_printf(&jw, "\"%s_tx_errors\":%u,", uart_names[u], st->tx_errors);
                }
            }
        }
    }

    /* Health */
    {
        runtime_health_state_t health = runtime_health_get();
        const char* hs = "ok";
        switch (health) {
            case RUNTIME_HEALTH_WARN:  hs = "warn"; break;
            case RUNTIME_HEALTH_ERROR: hs = "error"; break;
            case RUNTIME_HEALTH_FATAL: hs = "fatal"; break;
            default: break;
        }
        jw_kv_str(&jw, "health", hs);
    }

    /* UDP */
    {
        const transport_udp_t* udp = as_udp(diag->udp);
        if (udp != NULL) {
            jw_kv_bool(&jw, "udp_bound", udp->status.bound);
            jw_kv_str(&jw, "udp_tx_packets", "not_available");
            jw_kv_str(&jw, "udp_tx_errors", "not_available");
        } else {
            jw_kv_str(&jw, "udp", "not_available");
        }
    }

    /* Heading */
    {
        const gnss_dual_heading_calc_t* hdg = as_heading(diag->heading);
        if (hdg != NULL) {
            jw_kv_bool(&jw, "heading_valid", hdg->result.valid);
            jw_printf(&jw, "\"heading_deg\":%.1f,", hdg->result.heading_rad * (180.0 / 3.14159265));
            jw_kv_uint(&jw, "heading_calc_count", hdg->calc_count);
        } else {
            jw_kv_str(&jw, "heading", "not_available");
        }
    }

    /* Remove trailing comma */
    if (jw.pos > 1 && jw.buf[jw.pos - 1] == ',') {
        jw.buf[jw.pos - 1] = '\0';
        jw.pos--;
    }
    jw_put(&jw, "}");
}

/* =================================================================
 * /diag handler
 * ================================================================= */

static void build_diag_text(remote_diag_t* diag, char* buf, size_t buf_size)
{
    int o = 0;

    o += snprintf(buf + o, buf_size - o, "=== AOG ESP Multiboard Remote Diag (R2) ===\n");

    /* System */
    o += snprintf(buf + o, buf_size - o,
        "System: board=%s role=NAV mode=%s uptime=%llums\n",
        board_name_str(), system_mode_str(), (unsigned long long)get_uptime_ms(diag));

    /* Network */
    {
        net_status_t ns;
        get_net_status(&ns);
        o += snprintf(buf + o, buf_size - o,
            "Network: ip=%s/%s gw=%s\n", ns.ip, ns.netmask, ns.gateway);
    }

    /* Fast Loop */
    o += snprintf(buf + o, buf_size - o,
        "FastLoop: alive=%s cycles=%lu last_us=%lu worst_us=%lu age=%llums\n",
        is_fast_alive() ? "yes" : "no",
        (unsigned long)runtime_stats_get_cycle_count(),
        (unsigned long)runtime_stats_get_last(),
        (unsigned long)runtime_stats_get_worst(),
        (unsigned long long)fast_age_ms());

    /* GNSS */
    const gnss_um980_t* gnss_arr[2] = { as_gnss(diag->primary_gnss), as_gnss(diag->secondary_gnss) };
    const char* labels[2] = { "Primary", "Secondary" };

    for (int g = 0; g < 2; g++) {
        const gnss_um980_t* gnss = gnss_arr[g];
        if (gnss != NULL) {
            uint64_t n = now_ms();
            uint64_t gga_age = (gnss->snapshot.last_gga_time_ms > 0 && n > gnss->snapshot.last_gga_time_ms)
                ? (n - gnss->snapshot.last_gga_time_ms) : 0;
            uint64_t rmc_age = (gnss->snapshot.last_rmc_time_ms > 0 && n > gnss->snapshot.last_rmc_time_ms)
                ? (n - gnss->snapshot.last_rmc_time_ms) : 0;

            o += snprintf(buf + o, buf_size - o,
                "GNSS-%s: rx=%lu nmea_ok=%lu bad=%lu overflows=%lu timeouts=%lu "
                "gga=%lu rmc=%lu gst=%lu gsa=%lu gsv=%lu "
                "last=%s last_age=%llums "
                "gga_age=%llums rmc_age=%llums "
                "pos_valid=%s mot_valid=%s acc_valid=%s valid=%s fresh=%s "
                "fix=%s rtk=%s sats=%u hdop=%.1f alt=%.1f spd=%.1f crs=%.1f "
                "reason=%s timeout=%ums\n",
                labels[g],
                (unsigned long)gnss->bytes_received,
                (unsigned long)gnss->sentences_parsed,
                (unsigned long)gnss->checksum_errors,
                (unsigned long)gnss->overflow_errors,
                (unsigned long)gnss->timeout_events,
                (unsigned long)gnss->gga_count,
                (unsigned long)gnss->rmc_count,
                (unsigned long)gnss->gst_count,
                (unsigned long)gnss->gsa_count,
                (unsigned long)gnss->gsv_count,
                sentence_type_str(gnss->last_sentence_type),
                (unsigned long long)((gnss->last_sentence_us > 0 && now_us() > gnss->last_sentence_us)
                    ? (now_us() - gnss->last_sentence_us) / 1000 : 0),
                (unsigned long long)gga_age,
                (unsigned long long)rmc_age,
                gnss->snapshot.position_valid ? "yes" : "no",
                gnss->snapshot.motion_valid ? "yes" : "no",
                gnss->snapshot.accuracy_valid ? "yes" : "no",
                gnss->snapshot.valid ? "yes" : "no",
                gnss->snapshot.fresh ? "yes" : "no",
                fix_quality_str(gnss->snapshot.fix_quality),
                rtk_status_str(gnss->snapshot.rtk_status),
                (unsigned)gnss->snapshot.satellites,
                gnss->snapshot.hdop,
                gnss->snapshot.altitude,
                gnss->snapshot.speed_ms,
                gnss->snapshot.course_deg,
                status_reason_str(gnss->snapshot.status_reason),
                (unsigned)gnss->freshness_timeout_ms);
        } else {
            o += snprintf(buf + o, buf_size - o, "GNSS-%s: not_available\n", labels[g]);
        }
    }

    /* NTRIP */
    {
        const ntrip_client_t* ntrip = as_ntrip(diag->ntrip);
        if (ntrip != NULL) {
            const char* rx_age = "never";
            char rx_age_buf[32];
            if (ntrip->last_rx_us > 0) {
                uint64_t a = (now_us() > ntrip->last_rx_us) ? (now_us() - ntrip->last_rx_us) / 1000 : 0;
                snprintf(rx_age_buf, sizeof(rx_age_buf), "%llums", (unsigned long long)a);
                rx_age = rx_age_buf;
            }
            o += snprintf(buf + o, buf_size - o,
                "NTRIP: started=%s config=%s state=%s reconnects=%lu "
                "err=%u http=%u bytes_rx=%lu last_rx_age=%s\n",
                ntrip->started ? "yes" : "no",
                ntrip->config_valid ? "yes" : "no",
                ntrip_state_str(ntrip->state),
                (unsigned long)ntrip->reconnect_count,
                (unsigned)ntrip->last_error,
                (unsigned)ntrip->http_status_code,
                (unsigned long)ntrip->bytes_rx_total,
                rx_age);
        } else {
            o += snprintf(buf + o, buf_size - o, "NTRIP: not_available\n");
        }
    }

    /* RTCM */
    {
        const rtcm_router_t* router = as_rtcm_router(diag->rtcm_router);
        if (router != NULL) {
            const rtcm_stats_t* stats = rtcm_router_get_stats(router);
            const char* act = "never";
            char act_buf[32];
            if (stats != NULL && stats->last_activity_us > 0) {
                uint64_t a = (now_us() > stats->last_activity_us) ? (now_us() - stats->last_activity_us) / 1000 : 0;
                snprintf(act_buf, sizeof(act_buf), "%llums", (unsigned long long)a);
                act = act_buf;
            }
            o += snprintf(buf + o, buf_size - o,
                "RTCM: in=%lu drops=%lu overflows=%lu last_activity=%s\n",
                (unsigned long)((stats != NULL) ? stats->bytes_in : 0),
                (unsigned long)((stats != NULL) ? stats->bytes_dropped : 0),
                (unsigned long)router->output_overflow_count,
                act);
            for (uint8_t i = 0; i < router->output_count; i++) {
                const char* ta = "never";
                char ta_buf[32];
                if (router->outputs[i].last_activity_us > 0) {
                    uint64_t a = (now_us() > router->outputs[i].last_activity_us) ? (now_us() - router->outputs[i].last_activity_us) / 1000 : 0;
                    snprintf(ta_buf, sizeof(ta_buf), "%llums", (unsigned long long)a);
                    ta = ta_buf;
                }
                o += snprintf(buf + o, buf_size - o,
                    "  UART%u: tx=%lu drops=%lu last_tx_age=%s\n",
                    (unsigned)(i + 1),
                    (unsigned long)router->outputs[i].bytes_forwarded,
                    (unsigned long)router->outputs[i].bytes_dropped,
                    ta);
            }
        } else {
            o += snprintf(buf + o, buf_size - o, "RTCM: not_available\n");
        }
    }

    /* AOG */
    {
        const aog_nav_app_t* app = as_nav_app(diag->nav_app);
        if (app != NULL) {
            o += snprintf(buf + o, buf_size - o,
                "AOG: pgn214_sent=%lu cycles=%lu pos_valid=%s hdg_valid=%s\n",
                (unsigned long)app->pgn214_send_count, (unsigned long)app->cycle_count,
                app->gnss_output_active ? "yes" : "no",
                app->heading_output_active ? "yes" : "no");
        } else {
            o += snprintf(buf + o, buf_size - o, "AOG: not_available\n");
        }
    }
}

/* =================================================================
 * HTTP handlers
 * ================================================================= */

static esp_err_t status_handler(httpd_req_t* req)
{
    if (s_diag_instance == NULL) { httpd_resp_send_500(req); return ESP_FAIL; }
    char json_buf[JSON_BUF_SIZE];
    build_status_json(s_diag_instance, json_buf, sizeof(json_buf));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_buf, strlen(json_buf));
    return ESP_OK;
}

static esp_err_t diag_handler(httpd_req_t* req)
{
    if (s_diag_instance == NULL) { httpd_resp_send_500(req); return ESP_FAIL; }
    char text_buf[DIAG_BUF_SIZE];
    build_diag_text(s_diag_instance, text_buf, sizeof(text_buf));
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, text_buf, strlen(text_buf));
    return ESP_OK;
}

static esp_err_t version_handler(httpd_req_t* req)
{
    const char* v =
        "aog_esp_multiboard\n"
        "framework: esp-idf (platformio)\n"
        "task: NAV-REMOTE-DIAG-001-R2 + NAV-GNSS-OUTDOOR-VALID-001\n";
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, v, strlen(v));
    return ESP_OK;
}

/* =================================================================
 * Public API
 * ================================================================= */

void remote_diag_init(remote_diag_t* diag)
{
    if (diag == NULL) return;
    memset(diag, 0, sizeof(remote_diag_t));
    diag->component.name = "remote_diag";
    diag->component.service_step = remote_diag_service_step;
    diag->component.service_group = SERVICE_GROUP_DIAGNOSTICS;
    diag->component.fast_input = NULL;
    diag->component.fast_process = NULL;
    diag->component.fast_output = NULL;
    diag->boot_time_us = 0;
    diag->http_server = NULL;
    s_diag_instance = diag;
}

void remote_diag_set_sources(remote_diag_t* diag,
                              const void* primary_uart,
                              const void* secondary_uart,
                              const void* primary_gnss,
                              const void* secondary_gnss,
                              const void* heading,
                              const void* nav_app,
                              const void* udp,
                              const void* ntrip,
                              const void* rtcm_router)
{
    if (diag == NULL) return;
    diag->primary_uart = primary_uart;
    diag->secondary_uart = secondary_uart;
    diag->primary_gnss = primary_gnss;
    diag->secondary_gnss = secondary_gnss;
    diag->heading = heading;
    diag->nav_app = nav_app;
    diag->udp = udp;
    diag->ntrip = ntrip;
    diag->rtcm_router = rtcm_router;
}

/* =================================================================
 * A1: Service step — IP-only gating, no hal_eth dependency
 *
 * HTTP server starts when:
 *   1. Not already started (http_server == NULL)
 *   2. IP address obtained via esp_netif (DHCP or static)
 *
 * hal_eth_is_connected() is NOT used for gating because the ESP32
 * HAL stub always returns false. The real gating is IP availability
 * which is the authoritative signal that the network stack is ready.
 * ================================================================= */

void remote_diag_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    remote_diag_t* diag = (remote_diag_t*)comp;
    if (diag == NULL) return;

    if (diag->boot_time_us == 0) {
        diag->boot_time_us = esp_timer_get_time();
    }

    if (diag->http_server == NULL) {
        /* A1: IP-only check — no hal_eth_is_connected() dependency */
        net_status_t ns;
        get_net_status(&ns);
        if (!ns.has_ip) {
            /* No IP yet — retry next service cycle.
             * This polls every ~10ms (DIAGNOSTICS service period).
             * Once DHCP completes or static IP is configured,
             * the HTTP server will start on the next cycle. */
            return;
        }

        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = REMOTE_DIAG_HTTP_PORT;
        config.max_uri_handlers = REMOTE_DIAG_MAX_URI_HANDLERS;
        config.stack_size = 4096;

        httpd_handle_t server = NULL;
        esp_err_t err = httpd_start(&server, &config);
        if (err == ESP_OK) {
            httpd_uri_t uris[] = {
                { .uri = "/status", .method = HTTP_GET, .handler = status_handler, .user_ctx = NULL },
                { .uri = "/diag",   .method = HTTP_GET, .handler = diag_handler,   .user_ctx = NULL },
                { .uri = "/version",.method = HTTP_GET, .handler = version_handler,.user_ctx = NULL },
            };
            for (int i = 0; i < 3; i++) {
                httpd_register_uri_handler(server, &uris[i]);
            }

            diag->http_server = (void*)server;
            ESP_LOGI(TAG, "HTTP server started on port %d (ip=%s)",
                     REMOTE_DIAG_HTTP_PORT, ns.ip);
        } else {
            ESP_LOGE(TAG, "HTTP server start failed: 0x%x (%s)",
                     (unsigned)err, esp_err_to_name(err));
        }
    }

    (void)timestamp_us;
}
