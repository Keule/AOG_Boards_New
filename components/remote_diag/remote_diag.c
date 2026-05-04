/* ========================================================================
 * remote_diag.c — Remote Diagnostics over Ethernet (NAV-REMOTE-DIAG-001-R2)
 *
 * Minimal HTTP server using ESP-IDF esp_http_server.
 * Provides /status, /diag, /version endpoints as JSON/text.
 *
 * 013_REMOTE_MAINTENANCE-002 additions:
 *   - GET /ota/status — OTA capability, partitions, last result
 *   - POST /ota        — Firmware binary upload (raw body, auto-reboot)
 *   - POST /reboot     — Controlled reboot
 *   - GET /            — Index page with all endpoint links
 *   - Extended /version with partition info
 *   - Extended /status with OTA state
 *   - Extended /diag with OTA state
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
#include "esp_ota_ops.h"

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

/* ---- OTA support ---- */
#include "hal_ota.h"

/* ---- Remote log support (NAV-REMOTE-LOG-001) ---- */
#include "remote_log.h"
#include "gnss_um980_snapshot.h"
#include "gnss_um980_control.h"
#include <stdlib.h>

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
    uint32_t last = runtime_stats_get_last();
    if (last == 0) return false;
    /* last is in microseconds; check age in ms */
    uint64_t last_us = (uint64_t)last;
    uint64_t age = (now_us() > last_us) ? (now_us() - last_us) / 1000 : 0;
    return (age < (uint64_t)REMOTE_DIAG_FAST_ALIVE_MS);
}

static uint64_t fast_age_ms(void)
{
    uint32_t last = runtime_stats_get_last();
    if (last == 0) return 0;
    uint64_t last_us = (uint64_t)last;
    return (now_us() > last_us) ? (now_us() - last_us) / 1000 : 0;
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
 * /status handler (extended with OTA state)
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
        jw_kv_str(&jw, "fast_cycle_count", "not_available");
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
            jw_kv_str(&jw, "pgn214_last_send_age_ms", "not_available");
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
            jw_kv_bool(&jw, "udp_bound", transport_udp_is_bound(udp));
            jw_kv_str(&jw, "udp_tx_packets", "not_available");
            jw_kv_str(&jw, "udp_tx_errors", "not_available");
        } else {
            jw_kv_str(&jw, "udp", "not_available");
        }
    }

    /* Heading — uses gnss_dual_heading_t (012 base API).
     * heading_rad is radians; convert to degrees for JSON output.
     */
    {
        const gnss_dual_heading_calc_t* hdg = as_heading(diag->heading);
        if (hdg != NULL) {
            const gnss_dual_heading_t* hs = gnss_dual_heading_get(hdg);
            if (hs != NULL) {
                jw_kv_bool(&jw, "heading_valid", hs->valid);
                jw_printf(&jw, "\"heading_deg\":%.1f,", hs->heading_rad * 57.2957795131);
                jw_kv_uint(&jw, "heading_calc_count", hdg->calc_count);
            } else {
                jw_kv_str(&jw, "heading_valid", "not_available");
            }
        } else {
            jw_kv_str(&jw, "heading", "not_available");
        }
    }

    /* ---- OTA State (013) ---- */
    jw_kv_bool(&jw, "ota_supported", hal_ota_is_supported());
    jw_kv_bool(&jw, "ota_active", hal_ota_is_in_progress());
    jw_kv_str(&jw, "ota_running_partition", hal_ota_get_active_partition_label());
    jw_kv_str(&jw, "ota_next_partition", hal_ota_get_next_partition_label());
    jw_kv_str(&jw, "ota_last_result", hal_ota_get_state_str());
    jw_printf(&jw, "\"ota_last_error\":0x%x,", (unsigned)hal_ota_get_last_error());
    jw_printf(&jw, "\"ota_bytes_received\":%llu,", (unsigned long long)hal_ota_get_bytes_written());
    jw_printf(&jw, "\"ota_image_size\":%llu,", (unsigned long long)hal_ota_get_bytes_total());
    jw_kv_bool(&jw, "reboot_pending", hal_ota_is_reboot_pending());

    /* Remove trailing comma */
    if (jw.pos > 1 && jw.buf[jw.pos - 1] == ',') {
        jw.buf[jw.pos - 1] = '\0';
        jw.pos--;
    }
    jw_put(&jw, "}");
}

/* =================================================================
 * /diag handler (extended with OTA state)
 * ================================================================= */

static void build_diag_text(remote_diag_t* diag, char* buf, size_t buf_size)
{
    int o = 0;

    o += snprintf(buf + o, buf_size - o, "=== AOG ESP Multiboard Remote Diag (R2 + OTA) ===\n");

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
        "FastLoop: alive=%s last_us=%lu worst_us=%lu age=%llums\n",
        is_fast_alive() ? "yes" : "no",
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
                "AOG: pgn214_sent=%lu cycles=%lu\n",
                (unsigned long)app->pgn214_send_count, (unsigned long)app->cycle_count);
        } else {
            o += snprintf(buf + o, buf_size - o, "AOG: not_available\n");
        }
    }

    /* OTA State (013) */
    o += snprintf(buf + o, buf_size - o,
        "OTA: supported=%s state=%s active=%s "
        "running_partition=%s next_partition=%s "
        "last_error=0x%x bytes_written=%llu image_size=%llu reboot_pending=%s\n",
        hal_ota_is_supported() ? "yes" : "no",
        hal_ota_get_state_str(),
        hal_ota_is_in_progress() ? "yes" : "no",
        hal_ota_get_active_partition_label(),
        hal_ota_get_next_partition_label(),
        (unsigned)hal_ota_get_last_error(),
        (unsigned long long)hal_ota_get_bytes_written(),
        (unsigned long long)hal_ota_get_bytes_total(),
        hal_ota_is_reboot_pending() ? "yes" : "no");
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
    char buf[512];
    int o = 0;

    const esp_partition_t* running = esp_ota_get_running_partition();
    const char* part_label = (running != NULL) ? running->label : "unknown";

    o += snprintf(buf + o, sizeof(buf) - o,
        "aog_esp_multiboard\n"
        "framework: esp-idf (platformio)\n"
        "task: NAV-REMOTE-DIAG-001-R2 + 013-OTA\n"
        "partition: %s\n", part_label);

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, buf, strlen(buf));
    return ESP_OK;
}

/* =================================================================
 * OTA endpoints (013_REMOTE_MAINTENANCE-002)
 * ================================================================= */

static esp_err_t ota_status_handler(httpd_req_t* req)
{
    char buf[1024];
    int o = 0;

    const esp_partition_t* running = esp_ota_get_running_partition();
    const char* running_label = (running != NULL) ? running->label : "unknown";
    const char* next_label = hal_ota_get_next_partition_label();

    o += snprintf(buf + o, sizeof(buf) - o,
        "{\n"
        "  \"ota_supported\": %s,\n"
        "  \"ota_ready\": %s,\n"
        "  \"ota_active\": %s,\n"
        "  \"ota_running_partition\": \"%s\",\n"
        "  \"ota_next_partition\": \"%s\",\n"
        "  \"ota_last_result\": \"%s\",\n"
        "  \"ota_last_error\": 0x%x,\n"
        "  \"ota_bytes_received\": %llu,\n"
        "  \"ota_image_size\": %llu,\n"
        "  \"reboot_pending\": %s\n"
        "}\n",
        hal_ota_is_supported() ? "true" : "false",
        (!hal_ota_is_in_progress() && !hal_ota_is_reboot_pending()) ? "true" : "false",
        hal_ota_is_in_progress() ? "true" : "false",
        running_label,
        next_label,
        hal_ota_get_state_str(),
        (unsigned)hal_ota_get_last_error(),
        (unsigned long long)hal_ota_get_bytes_written(),
        (unsigned long long)hal_ota_get_bytes_total(),
        hal_ota_is_reboot_pending() ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, strlen(buf));
    return ESP_OK;
}

static esp_err_t ota_upload_handler(httpd_req_t* req)
{
    if (hal_ota_is_in_progress()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA already in progress");
        return ESP_FAIL;
    }

    /* Read Content-Length to know image size */
    int content_len = req->content_len;
    if (content_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid Content-Length");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA upload started: expected=%d bytes", content_len);

    /* Begin OTA update */
    hal_err_t herr = hal_ota_begin((size_t)content_len);
    if (herr != HAL_OK) {
        char msg[128];
        snprintf(msg, sizeof(msg), "OTA begin failed: err=%d last_esp_err=0x%x",
                 (int)herr, (unsigned)hal_ota_get_last_error());
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, msg);
        return ESP_FAIL;
    }

    /* Read body in chunks and write to OTA partition */
    uint8_t buf[REMOTE_DIAG_OTA_BUF_SIZE];
    int remaining = content_len;

    while (remaining > 0) {
        int to_read = (remaining > (int)sizeof(buf)) ? (int)sizeof(buf) : remaining;
        int received = httpd_req_recv(req, (char*)buf, to_read);

        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Timeout — retry */
                continue;
            }
            ESP_LOGE(TAG, "OTA upload aborted: received=%d", received);
            hal_ota_end();
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "OTA upload aborted: connection lost");
            return ESP_FAIL;
        }

        herr = hal_ota_write(buf, (size_t)received);
        if (herr != HAL_OK) {
            char msg[128];
            snprintf(msg, sizeof(msg), "OTA write failed: err=%d esp_err=0x%x",
                     (int)herr, (unsigned)hal_ota_get_last_error());
            ESP_LOGE(TAG, "%s", msg);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, msg);
            return ESP_FAIL;
        }

        remaining -= received;
    }

    /* Finalize OTA */
    herr = hal_ota_end();
    if (herr != HAL_OK) {
        char msg[128];
        snprintf(msg, sizeof(msg), "OTA end failed: err=%d esp_err=0x%x",
                 (int)herr, (unsigned)hal_ota_get_last_error());
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, msg);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA upload complete: %d bytes written. Reboot pending.", content_len);

    char resp_buf[256];
    snprintf(resp_buf, sizeof(resp_buf),
        "{\n"
        "  \"ota_result\": \"ok\",\n"
        "  \"bytes_received\": %d,\n"
        "  \"reboot_pending\": true,\n"
        "  \"message\": \"OTA successful. Rebooting in 2 seconds.\"\n"
        "}\n",
        content_len);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_buf, strlen(resp_buf));

    ESP_LOGW(TAG, "OTA complete — rebooting in 2 seconds");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ESP_OK;
}

static esp_err_t reboot_handler(httpd_req_t* req)
{
    ESP_LOGW(TAG, "Reboot requested via /reboot endpoint");

    const char* resp =
        "{\n"
        "  \"result\": \"rebooting\",\n"
        "  \"message\": \"System will reboot in 1 second.\"\n"
        "}\n";

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK;
}

/* =================================================================
 * / (index) handler — Mini page with all endpoint links
 * ================================================================= */

static esp_err_t index_handler(httpd_req_t* req)
{
    const char* html =
        "<!DOCTYPE html>\n"
        "<html lang='en'>\n"
        "<head>\n"
        "<meta charset='utf-8'>\n"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>\n"
        "<title>AOG ESP Multiboard</title>\n"
        "<style>\n"
        "*{margin:0;padding:0;box-sizing:border-box}\n"
        "body{font-family:'Segoe UI',system-ui,sans-serif;background:#0f172a;color:#e2e8f0;min-height:100vh}\n"
        ".container{max-width:800px;margin:0 auto;padding:24px 16px}\n"
        "h1{font-size:1.4rem;margin-bottom:4px;color:#38bdf8}\n"
        ".sub{font-size:0.8rem;color:#94a3b8;margin-bottom:24px}\n"
        "h2{font-size:0.85rem;text-transform:uppercase;letter-spacing:1px;color:#64748b;margin:20px 0 8px;padding-bottom:4px;border-bottom:1px solid #1e293b}\n"
        "table{width:100%;border-collapse:collapse}\n"
        "td{padding:6px 8px;font-size:0.82rem;border-bottom:1px solid #1e293b}\n"
        "td:first-child{font-family:'SF Mono',Consolas,monospace;color:#38bdf8;white-space:nowrap}\n"
        "td:nth-child(2){color:#94a3b8;width:80px}\n"
        "td:last-child{color:#cbd5e1}\n"
        "a{color:#38bdf8;text-decoration:none}\n"
        "a:hover{text-decoration:underline}\n"
        ".danger{color:#f87171}\n"
        ".warn{color:#fbbf24}\n"
        ".badge{display:inline-block;font-size:0.65rem;padding:1px 6px;border-radius:3px;font-weight:600}\n"
        ".badge-get{background:#164e63;color:#67e8f9}\n"
        ".badge-post{background:#7c2d12;color:#fdba74}\n"
        "</style>\n"
        "</head>\n"
        "<body>\n"
        "<div class='container'>\n"
        "<h1>AOG ESP Multiboard</h1>\n"
        "<div class='sub'>Firmware Remote Interface &mdash; Navigation Board</div>\n"
        "\n"
        "<h2>System</h2>\n"
        "<table>\n"
        "<tr><td><a href='/status'>/status</a></td><td><span class='badge badge-get'>GET</span></td><td>Full JSON status (GNSS, NTRIP, RTCM, OTA, network)</td></tr>\n"
        "<tr><td><a href='/diag'>/diag</a></td><td><span class='badge badge-get'>GET</span></td><td>Plain-text diagnostic dump</td></tr>\n"
        "<tr><td><a href='/version'>/version</a></td><td><span class='badge badge-get'>GET</span></td><td>Firmware version &amp; partition info</td></tr>\n"
        "<tr><td><a href='/reboot' class='danger'>/reboot</a></td><td><span class='badge badge-get'>GET</span></td><td class='danger'>Restart ESP32 (1s delay)</td></tr>\n"
        "</table>\n"
        "\n"
        "<h2>OTA Firmware Update</h2>\n"
        "<table>\n"
        "<tr><td><a href='/ota/status'>/ota/status</a></td><td><span class='badge badge-get'>GET</span></td><td>OTA partition &amp; state info</td></tr>\n"
        "<tr><td>/ota</td><td><span class='badge badge-post'>POST</span></td><td>Upload firmware binary (auto-reboot)</td></tr>\n"
        "</table>\n"
        "\n"
        "<h2>Remote Log</h2>\n"
        "<table>\n"
        "<tr><td><a href='/logs'>/logs</a></td><td><span class='badge badge-get'>GET</span></td><td>Full log buffer (text)</td></tr>\n"
        "<tr><td><a href='/logs?tail=50'>/logs?tail=N</a></td><td><span class='badge badge-get'>GET</span></td><td>Last N lines from ringbuffer</td></tr>\n"
        "<tr><td><a href='/logs/status'>/logs/status</a></td><td><span class='badge badge-get'>GET</span></td><td>Log buffer stats (JSON)</td></tr>\n"
        "<tr><td><a href='/logs/clear'>/logs/clear</a></td><td><span class='badge badge-post'>POST</span></td><td>Clear log ringbuffer</td></tr>\n"
        "<tr><td><a href='/logs/view'>/logs/view</a></td><td><span class='badge badge-get'>GET</span></td><td>Auto-refreshing HTML log viewer</td></tr>\n"
        "</table>\n"
        "\n"
        "<h2>GNSS Configuration</h2>\n"
        "<table>\n"
        "<tr><td><a href='/gnss/config_snapshot'>/gnss/config_snapshot</a></td><td><span class='badge badge-get'>GET</span></td><td>Boot snapshot (both receivers)</td></tr>\n"
        "<tr><td><a href='/gnss/1/config_snapshot'>/gnss/1/config_snapshot</a></td><td><span class='badge badge-get'>GET</span></td><td>Boot snapshot receiver 1</td></tr>\n"
        "<tr><td><a href='/gnss/2/config_snapshot'>/gnss/2/config_snapshot</a></td><td><span class='badge badge-get'>GET</span></td><td>Boot snapshot receiver 2</td></tr>\n"
        "<tr><td><a href='/gnss/config_status'>/gnss/config_status</a></td><td><span class='badge badge-get'>GET</span></td><td>Snapshot status (JSON)</td></tr>\n"
        "</table>\n"
        "\n"
        "<h2>GNSS Commands (Wildcard /gnss/*)</h2>\n"
        "<table>\n"
        "<tr><td><a href='/gnss/config'>/gnss/config</a></td><td><span class='badge badge-get'>GET</span></td><td>Live query VERSIONA+CONFIG+MODE+MASK (both)</td></tr>\n"
        "<tr><td><a href='/gnss/1/config'>/gnss/1/config</a></td><td><span class='badge badge-get'>GET</span></td><td>Live config query receiver 1</td></tr>\n"
        "<tr><td><a href='/gnss/2/config'>/gnss/2/config</a></td><td><span class='badge badge-get'>GET</span></td><td>Live config query receiver 2</td></tr>\n"
        "<tr><td><a href='/gnss/1/status'>/gnss/1/status</a></td><td><span class='badge badge-get'>GET</span></td><td>Control layer stats receiver 1</td></tr>\n"
        "<tr><td><a href='/gnss/2/status'>/gnss/2/status</a></td><td><span class='badge badge-get'>GET</span></td><td>Control layer stats receiver 2</td></tr>\n"
        "<tr><td><a href='/gnss/1/send/VERSIONA'>/gnss/1/send/&lt;cmd&gt;</a></td><td><span class='badge badge-get'>GET</span></td><td>Send command to receiver 1</td></tr>\n"
        "<tr><td><a href='/gnss/2/send/VERSIONA'>/gnss/2/send/&lt;cmd&gt;</a></td><td><span class='badge badge-get'>GET</span></td><td>Send command to receiver 2</td></tr>\n"
        "<tr><td><a href='/gnss/1/unlogall' class='warn'>/gnss/1/unlogall</a></td><td><span class='badge badge-get'>GET</span></td><td class='warn'>UNLOGALL receiver 1</td></tr>\n"
        "<tr><td><a href='/gnss/2/unlogall' class='warn'>/gnss/2/unlogall</a></td><td><span class='badge badge-get'>GET</span></td><td class='warn'>UNLOGALL receiver 2</td></tr>\n"
        "</table>\n"
        "\n"
        "</div>\n"
        "</body>\n"
        "</html>\n";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

/* =================================================================
 * Remote Log endpoints (NAV-REMOTE-LOG-001)
 * ================================================================= */

static esp_err_t logs_handler(httpd_req_t* req)
{
    /* Check for ?tail=N query parameter */
    char buf[256];
    int qlen = httpd_req_get_url_query_len(req);
    uint32_t tail_lines = 0;

    if (qlen > 0) {
        if (qlen < (int)sizeof(buf)) {
            httpd_req_get_url_query_str(req, buf, sizeof(buf));
            char* tail_param = strstr(buf, "tail=");
            if (tail_param != NULL) {
                tail_param += 5;
                long val = strtol(tail_param, NULL, 10);
                if (val > 0) tail_lines = (uint32_t)val;
            }
        }
    }

    /* Allocate response buffer — use the ringbuffer size + 1 for null terminator */
    size_t log_size = 32768;
    char* log_buf = malloc(log_size);
    if (log_buf == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    size_t written;
    if (tail_lines > 0) {
        written = remote_log_read_tail_lines(log_buf, log_size, tail_lines);
    } else {
        written = remote_log_read(log_buf, log_size);
    }

    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    esp_err_t err = httpd_resp_send(req, log_buf, written);
    free(log_buf);
    return err;
}

static esp_err_t logs_status_handler(httpd_req_t* req)
{
    remote_log_stats_t stats;
    remote_log_get_stats(&stats);

    char buf[768];
    int o = 0;
    o += snprintf(buf + o, sizeof(buf) - o,
        "{\n"
        "  \"remote_log_enabled\": %s,\n"
        "  \"early_init\": %s,\n"
        "  \"fully_initialized\": %s,\n"
        "  \"buffer_size\": %u,\n"
        "  \"used_bytes\": %u,\n"
        "  \"lines_written\": %u,\n"
        "  \"lines_dropped\": %u,\n"
        "  \"bytes_written\": %u,\n"
        "  \"bytes_overwritten\": %u,\n"
        "  \"boot_lines_captured\": %u,\n"
        "  \"boot_bytes_captured\": %u\n"
        "}\n",
        remote_log_is_initialized() ? "true" : "false",
        stats.early_init_called ? "true" : "false",
        stats.fully_initialized ? "true" : "false",
        (unsigned)stats.buffer_size,
        (unsigned)stats.used_bytes,
        (unsigned)stats.lines_written,
        (unsigned)stats.lines_dropped,
        (unsigned)stats.bytes_written,
        (unsigned)stats.bytes_overwritten,
        (unsigned)stats.boot_lines_captured,
        (unsigned)stats.boot_bytes_captured);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, strlen(buf));
    return ESP_OK;
}

static esp_err_t logs_clear_handler(httpd_req_t* req)
{
    remote_log_clear();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", 10);
    return ESP_OK;
}

static esp_err_t logs_view_handler(httpd_req_t* req)
{
    const char* html =
        "<!doctype html>"
        "<html><head><meta charset='utf-8'>"
        "<title>AOG NAV Logs</title>"
        "<style>"
        "body{font-family:monospace;background:#111;color:#ddd;margin:0;padding:8px}"
        "pre{white-space:pre-wrap;word-break:break-word;font-size:13px;line-height:1.4}"
        "h1{font-size:16px;margin:0 0 8px;color:#4a4}"
        "</style></head><body>"
        "<h1>AOG NAV Logs</h1>"
        "<pre id='logs'>Loading...</pre>"
        "<script>"
        "async function refresh(){"
        "  try{"
        "    const r=await fetch('/logs?tail=200');"
        "    document.getElementById('logs').textContent=await r.text();"
        "  }catch(e){}"
        "}"
        "setInterval(refresh,1000);"
        "refresh();"
        "</script></body></html>";

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

/* =================================================================
 * GNSS Config Snapshot endpoints (NAV-UM980-CONFIG-SNAPSHOT-001)
 * ================================================================= */

static esp_err_t gnss_snapshot_handler(httpd_req_t* req)
{
    const char* data = gnss_um980_snapshot_get_combined();
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, data, strlen(data));
    return ESP_OK;
}

static esp_err_t gnss_snapshot_rx1_handler(httpd_req_t* req)
{
    const char* data = gnss_um980_snapshot_get_receiver1();
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, data, strlen(data));
    return ESP_OK;
}

static esp_err_t gnss_snapshot_rx2_handler(httpd_req_t* req)
{
    const char* data = gnss_um980_snapshot_get_receiver2();
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, data, strlen(data));
    return ESP_OK;
}

static esp_err_t gnss_snapshot_status_handler(httpd_req_t* req)
{
    gnss_um980_snapshot_status_t st;
    gnss_um980_snapshot_get_status(&st);

    char buf[512];
    int n = snprintf(buf, sizeof(buf),
        "{\n"
        "  \"complete\": %s,\n"
        "  \"rx1_complete\": %s,\n"
        "  \"rx2_complete\": %s,\n"
        "  \"rx1_timeout\": %s,\n"
        "  \"rx2_timeout\": %s,\n"
        "  \"rx1_bytes\": %zu,\n"
        "  \"rx2_bytes\": %zu,\n"
        "  \"duration_ms\": %u\n"
        "}\n",
        gnss_um980_snapshot_is_complete() ? "true" : "false",
        st.rx1_complete ? "true" : "false",
        st.rx2_complete ? "true" : "false",
        st.rx1_timeout ? "true" : "false",
        st.rx2_timeout ? "true" : "false",
        st.rx1_bytes,
        st.rx2_bytes,
        (unsigned)st.duration_ms);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, (n > 0) ? (size_t)n : 0);
    return ESP_OK;
}

/* =================================================================
 * GNSS Remote Command Interface (NAV-REMOTE-GNSS-CMD-001)
 *
 * Wildcard handler for /gnss/<n> paths:
 *   /gnss/1/send/<cmd>   — send arbitrary command to receiver 1/2
 *   /gnss/2/send/<cmd>   — send arbitrary command to receiver 1/2
 *   /gnss/1/config        — live query: version+config+mode+mask
 *   /gnss/2/config        — live query: version+config+mode+mask
 *   /gnss/config          — live query both receivers
 *   /gnss/1/status        — control layer status for receiver 1
 *   /gnss/2/status        — control layer status for receiver 2
 *   /gnss/1/unlogall      — send UNLOGALL to receiver 1
 *   /gnss/2/unlogall      — send UNLOGALL to receiver 2
 * ================================================================= */

/* JSON string escaping for raw UM980 response data */
static size_t json_escape_str(const char* src, size_t src_len, char* dst, size_t dst_size)
{
    size_t si = 0, di = 0;
    while (si < src_len && di < dst_size - 6) {
        unsigned char c = (unsigned char)src[si];
        switch (c) {
            case '"':  memcpy(dst + di, "\\\"", 2); di += 2; break;
            case '\\': memcpy(dst + di, "\\\\", 2); di += 2; break;
            case '\n': memcpy(dst + di, "\\n", 2);  di += 2; break;
            case '\r': memcpy(dst + di, "\\r", 2);  di += 2; break;
            case '\t': memcpy(dst + di, "\\t", 2);  di += 2; break;
            default:
                if (c < 0x20) {
                    di += (size_t)snprintf(dst + di, dst_size - di, "\\u%04x", c);
                } else {
                    dst[di++] = c;
                }
                break;
        }
        si++;
    }
    dst[di] = '\0';
    return di;
}

/* Error code to string */
static const char* ctrl_err_str(gnss_ctrl_err_t err)
{
    switch (err) {
        case GNSS_CTRL_OK:                 return "ok";
        case GNSS_CTRL_ERR_NOT_INITIALIZED: return "not_initialized";
        case GNSS_CTRL_ERR_INVALID_PARAM:   return "invalid_param";
        case GNSS_CTRL_ERR_TIMEOUT:         return "timeout";
        case GNSS_CTRL_ERR_UART_BUSY:       return "uart_busy";
        case GNSS_CTRL_ERR_TX_FAILED:       return "tx_failed";
        case GNSS_CTRL_ERR_BLOCKED:         return "blocked";
        case GNSS_CTRL_ERR_EXPECT_FAILED:   return "expect_failed";
        default:                            return "unknown";
    }
}

static esp_err_t gnss_cmd_handler(httpd_req_t* req)
{
    /* Parse URI: /gnss/<receiver>/<action>[/<param>] */
    const char* uri = req->uri;
    if (strncmp(uri, "/gnss/", 6) != 0) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    const char* p = uri + 6;  /* past "/gnss/" */

    /* Extract receiver number */
    int receiver = 0;
    if (p[0] == '1' && p[1] == '/') {
        receiver = 1; p += 2;
    } else if (p[0] == '2' && p[1] == '/') {
        receiver = 2; p += 2;
    } else {
        receiver = 0;  /* both */
    }

    /* Extract action */
    char action[32] = {0};
    {
        int ai = 0;
        while (*p && *p != '/' && ai < 31) {
            action[ai++] = *p++;
        }
        action[ai] = '\0';
    }

    /* Extract optional parameter (command string after last /) */
    char param[512] = {0};
    if (*p == '/') {
        p++;
        strncpy(param, p, sizeof(param) - 1);
        gnss_um980_url_decode(param, sizeof(param));
    }

    /* ---- Action: /gnss/<n>/send/<cmd> ---- */
    if (strcmp(action, "send") == 0 && receiver > 0 && param[0] != '\0') {
        gnss_um980_control_t* ctrl = gnss_um980_control_get(receiver);
        if (ctrl == NULL) {
            char err[128];
            snprintf(err, sizeof(err),
                "{\"success\":false,\"error\":\"receiver %d not available\"}", receiver);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, err, strlen(err));
            return ESP_OK;
        }

        char response[GNSS_CTRL_MAX_RESPONSE];
        uint32_t t_start = (uint32_t)(esp_timer_get_time() / 1000);
        gnss_ctrl_err_t err = gnss_um980_send_command(ctrl, param,
                                                      response, sizeof(response), 0);
        uint32_t t_duration = (uint32_t)(esp_timer_get_time() / 1000) - t_start;

        char json_buf[12288];
        char escaped[GNSS_CTRL_MAX_RESPONSE * 2];
        json_escape_str(response, strlen(response), escaped, sizeof(escaped));

        int n = snprintf(json_buf, sizeof(json_buf),
            "{\n"
            "  \"receiver\": %d,\n"
            "  \"command\": \"%s\",\n"
            "  \"success\": %s,\n"
            "  \"error\": \"%s\",\n"
            "  \"bytes\": %zu,\n"
            "  \"duration_ms\": %u,\n"
            "  \"response\": \"%s\"\n"
            "}\n",
            receiver, param,
            err == GNSS_CTRL_OK ? "true" : "false",
            ctrl_err_str(err),
            strlen(response),
            (unsigned)t_duration,
            escaped);

        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_buf, (n > 0) ? (size_t)n : 0);
        return ESP_OK;
    }

    /* ---- Action: /gnss/<n>/config (live query) ---- */
    if (strcmp(action, "config") == 0) {
        char text_buf[GNSS_CTRL_MAX_RESPONSE * 5];
        int pos = 0;
        int text_cap = (int)sizeof(text_buf) - 1;

        int rx_start = (receiver == 0) ? 1 : receiver;
        int rx_end   = (receiver == 0) ? 2 : receiver;

        for (int rx = rx_start; rx <= rx_end; rx++) {
            gnss_um980_control_t* ctrl = gnss_um980_control_get(rx);
            if (ctrl == NULL) {
                pos += snprintf(text_buf + pos, text_cap - pos,
                    "===== UM980 RECEIVER %d — NOT AVAILABLE =====\r\n\r\n", rx);
                continue;
            }

            pos += snprintf(text_buf + pos, text_cap - pos,
                "===== UM980 RECEIVER %d LIVE CONFIG =====\r\n", rx);

            static const char* const query_cmds[] = { "VERSIONA", "CONFIG", "MODE", "MASK" };
            for (int qi = 0; qi < 4; qi++) {
                char response[GNSS_CTRL_MAX_RESPONSE];
                gnss_ctrl_err_t err = gnss_um980_send_command(ctrl, query_cmds[qi],
                                                              response, sizeof(response), 2000);
                pos += snprintf(text_buf + pos, text_cap - pos, "> %s  %s\r\n", query_cmds[qi],
                             err == GNSS_CTRL_OK ? "[OK]" : ctrl_err_str(err));
                if (err == GNSS_CTRL_OK && response[0] != '\0') {
                    pos += snprintf(text_buf + pos, text_cap - pos, "%s\r\n\r\n", response);
                } else {
                    pos += snprintf(text_buf + pos, text_cap - pos, "\r\n");
                }
            }
            pos += snprintf(text_buf + pos, text_cap - pos, "===== END =====\r\n");
            if (rx < rx_end) {
                pos += snprintf(text_buf + pos, text_cap - pos, "\r\n");
            }
        }

        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, text_buf, (pos > 0) ? (size_t)pos : 0);
        return ESP_OK;
    }

    /* ---- Action: /gnss/<n>/status ---- */
    if (strcmp(action, "status") == 0) {
        char json[1024];

        if (receiver == 0) {
            gnss_um980_control_t* c1 = gnss_um980_control_get(1);
            gnss_um980_control_t* c2 = gnss_um980_control_get(2);
            snprintf(json, sizeof(json),
                "{\n"
                "  \"rx1_available\": %s,\n"
                "  \"rx2_available\": %s,\n"
                "  \"rx1_initialized\": %s,\n"
                "  \"rx2_initialized\": %s,\n"
                "  \"rx1_commands_sent\": %u,\n"
                "  \"rx1_commands_ok\": %u,\n"
                "  \"rx1_commands_timeout\": %u,\n"
                "  \"rx1_commands_blocked\": %u,\n"
                "  \"rx2_commands_sent\": %u,\n"
                "  \"rx2_commands_ok\": %u,\n"
                "  \"rx2_commands_timeout\": %u,\n"
                "  \"rx2_commands_blocked\": %u\n"
                "}\n",
                c1 ? "true" : "false", c2 ? "true" : "false",
                c1 && c1->initialized ? "true" : "false",
                c2 && c2->initialized ? "true" : "false",
                c1 ? (unsigned)c1->commands_sent : 0, c1 ? (unsigned)c1->commands_ok : 0,
                c1 ? (unsigned)c1->commands_timeout : 0, c1 ? (unsigned)c1->commands_blocked : 0,
                c2 ? (unsigned)c2->commands_sent : 0, c2 ? (unsigned)c2->commands_ok : 0,
                c2 ? (unsigned)c2->commands_timeout : 0, c2 ? (unsigned)c2->commands_blocked : 0);
        } else {
            gnss_um980_control_t* ctrl = gnss_um980_control_get(receiver);
            if (ctrl == NULL) {
                snprintf(json, sizeof(json),
                    "{\"receiver\":%d,\"available\":false}", receiver);
            } else {
                snprintf(json, sizeof(json),
                    "{\n"
                    "  \"receiver\": %d,\n"
                    "  \"available\": true,\n"
                    "  \"initialized\": %s,\n"
                    "  \"command_active\": %s,\n"
                    "  \"commands_sent\": %u,\n"
                    "  \"commands_ok\": %u,\n"
                    "  \"commands_timeout\": %u,\n"
                    "  \"commands_blocked\": %u\n"
                    "}\n",
                    receiver,
                    ctrl->initialized ? "true" : "false",
                    ctrl->command_active ? "true" : "false",
                    (unsigned)ctrl->commands_sent, (unsigned)ctrl->commands_ok,
                    (unsigned)ctrl->commands_timeout, (unsigned)ctrl->commands_blocked);
            }
        }

        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }

    /* ---- Action: /gnss/<n>/unlogall ---- */
    if (strcmp(action, "unlogall") == 0 && receiver > 0) {
        gnss_um980_control_t* ctrl = gnss_um980_control_get(receiver);
        if (ctrl == NULL) {
            char err[128];
            snprintf(err, sizeof(err),
                "{\"success\":false,\"error\":\"receiver %d not available\"}", receiver);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, err, strlen(err));
            return ESP_OK;
        }

        char response[256];
        gnss_ctrl_err_t err = gnss_um980_send_command(ctrl, "UNLOGALL",
                                                      response, sizeof(response), 2000);

        char json[512];
        snprintf(json, sizeof(json),
            "{\n"
            "  \"receiver\": %d,\n"
            "  \"command\": \"UNLOGALL\",\n"
            "  \"success\": %s,\n"
            "  \"error\": \"%s\"\n"
            "}\n",
            receiver,
            err == GNSS_CTRL_OK ? "true" : "false",
            ctrl_err_str(err));

        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }

    /* Unknown action */
    httpd_resp_send_404(req);
    return ESP_FAIL;
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

    /* Initialize HAL OTA with ESP32 ops */
    hal_ota_init(hal_ota_esp32_ops());
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
        /* Stack must be large enough for build_status_json (8KB json_buf)
         * plus json_writer_t (~24B) + net_status_t (~80B) + nested calls
         * + vsnprintf overhead.  4096 caused LoadProhibited crash.
         * 16384 gives comfortable headroom.  ESP32 has ~180 KB free heap. */
        config.stack_size = 16384;

        httpd_handle_t server = NULL;
        esp_err_t err = httpd_start(&server, &config);
        if (err == ESP_OK) {
            httpd_uri_t uris[] = {
                { .uri = "/",              .method = HTTP_GET,  .handler = index_handler,         .user_ctx = NULL },
                { .uri = "/status",        .method = HTTP_GET,  .handler = status_handler,        .user_ctx = NULL },
                { .uri = "/diag",          .method = HTTP_GET,  .handler = diag_handler,          .user_ctx = NULL },
                { .uri = "/version",       .method = HTTP_GET,  .handler = version_handler,       .user_ctx = NULL },
                { .uri = "/ota/status",    .method = HTTP_GET,  .handler = ota_status_handler,    .user_ctx = NULL },
                { .uri = "/ota",           .method = HTTP_POST, .handler = ota_upload_handler,    .user_ctx = NULL },
                { .uri = "/reboot",        .method = HTTP_GET,  .handler = reboot_handler,        .user_ctx = NULL },
                { .uri = "/logs",          .method = HTTP_GET,  .handler = logs_handler,          .user_ctx = NULL },
                { .uri = "/logs/status",   .method = HTTP_GET,  .handler = logs_status_handler,   .user_ctx = NULL },
                { .uri = "/logs/clear",    .method = HTTP_POST, .handler = logs_clear_handler,    .user_ctx = NULL },
                { .uri = "/logs/view",     .method = HTTP_GET,  .handler = logs_view_handler,     .user_ctx = NULL },
                /* NAV-UM980-CONFIG-SNAPSHOT-001: GNSS config snapshot endpoints */
                { .uri = "/gnss/config_snapshot",  .method = HTTP_GET,  .handler = gnss_snapshot_handler,     .user_ctx = NULL },
                { .uri = "/gnss/1/config_snapshot", .method = HTTP_GET,  .handler = gnss_snapshot_rx1_handler, .user_ctx = NULL },
                { .uri = "/gnss/2/config_snapshot", .method = HTTP_GET,  .handler = gnss_snapshot_rx2_handler, .user_ctx = NULL },
                { .uri = "/gnss/config_status",     .method = HTTP_GET,  .handler = gnss_snapshot_status_handler,.user_ctx = NULL },
                /* NAV-REMOTE-GNSS-CMD-001: GNSS remote command interface */
                /* Explicit URIs for reliability (ESP-IDF wildcard matching unreliable) */
                { .uri = "/gnss/config",           .method = HTTP_GET,  .handler = gnss_cmd_handler,          .user_ctx = NULL },
                { .uri = "/gnss/status",           .method = HTTP_GET,  .handler = gnss_cmd_handler,          .user_ctx = NULL },
                { .uri = "/gnss/1/config",         .method = HTTP_GET,  .handler = gnss_cmd_handler,          .user_ctx = NULL },
                { .uri = "/gnss/1/status",         .method = HTTP_GET,  .handler = gnss_cmd_handler,          .user_ctx = NULL },
                { .uri = "/gnss/1/unlogall",       .method = HTTP_GET,  .handler = gnss_cmd_handler,          .user_ctx = NULL },
                { .uri = "/gnss/2/config",         .method = HTTP_GET,  .handler = gnss_cmd_handler,          .user_ctx = NULL },
                { .uri = "/gnss/2/status",         .method = HTTP_GET,  .handler = gnss_cmd_handler,          .user_ctx = NULL },
                { .uri = "/gnss/2/unlogall",       .method = HTTP_GET,  .handler = gnss_cmd_handler,          .user_ctx = NULL },
                /* Wildcard fallback for parameterized /gnss/<n>/send/<cmd> */
                { .uri = "/gnss/*",                .method = HTTP_GET,  .handler = gnss_cmd_handler,          .user_ctx = NULL },
            };
            for (int i = 0; i < (int)(sizeof(uris) / sizeof(uris[0])); i++) {
                httpd_register_uri_handler(server, &uris[i]);
            }

            diag->http_server = (void*)server;
            ESP_LOGI(TAG, "HTTP server started on port %d (ip=%s, %d endpoints)",
                     REMOTE_DIAG_HTTP_PORT, ns.ip, (int)(sizeof(uris) / sizeof(uris[0])));
        } else {
            ESP_LOGE(TAG, "HTTP server start failed: 0x%x (%s)",
                     (unsigned)err, esp_err_to_name(err));
        }
    }

    (void)timestamp_us;
}
