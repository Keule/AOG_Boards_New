/* ========================================================================
 * remote_diag.c — Remote Diagnostics over Ethernet (NAV-REMOTE-DIAG-001)
 *
 * Minimal HTTP server using ESP-IDF esp_http_server.
 * Provides /status, /diag, /version endpoints as JSON/text.
 *
 * Architecture:
 *   - HTTP server started on first service_step after boot
 *   - Runs on Core 0 via SERVICE_GROUP_DIAGNOSTICS
 *   - No network code in task_fast (Core 1)
 *   - All component references are read-only (const void*)
 *   - Missing fields reported as "not_available" — endpoint never fails
 *
 * JSON response format is hand-crafted (no cJSON dependency).
 * All numeric values formatted via snprintf into a stack buffer.
 * ======================================================================== */

#include "remote_diag.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_http_server.h"
#include "esp_netif.h"

/* ---- Observed component headers (type-safe access) ---- */
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
#include "hal_eth.h"

static const char* TAG = "REMOTE_DIAG";

/* ---- JSON output buffer ---- */
#define JSON_BUF_SIZE  4096

/* ---- Helpers: safe pointer deref with NULL check ---- */

static const transport_uart_t* as_uart(const void* p)
{
    return (const transport_uart_t*)p;
}

static const gnss_um980_t* as_gnss(const void* p)
{
    return (const gnss_um980_t*)p;
}

static const gnss_dual_heading_calc_t* as_heading(const void* p)
{
    return (const gnss_dual_heading_calc_t*)p;
}

static const aog_nav_app_t* as_nav_app(const void* p)
{
    return (const aog_nav_app_t*)p;
}

static const transport_udp_t* as_udp(const void* p)
{
    return (const transport_udp_t*)p;
}

static const ntrip_client_t* as_ntrip(const void* p)
{
    return (const ntrip_client_t*)p;
}

static const rtcm_router_t* as_rtcm_router(const void* p)
{
    return (const rtcm_router_t*)p;
}

/* ---- Helpers: enum to string ---- */

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

static const char* system_mode_str(void)
{
    system_mode_t m = runtime_mode_get();
    return (m == SYSTEM_MODE_WORK) ? "work" : "config";
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

/* ---- Singleton pointer for HTTP handlers ----
 * The esp_http_server URI handler callback only receives an httpd_req_t*,
 * so we need a global pointer to our diag instance.
 * This is safe because there is exactly one remote_diag_t in the system. */

static remote_diag_t* s_diag_instance = NULL;

/* ---- JSON building helpers ---- */

typedef struct {
    char*  buf;
    size_t pos;
    size_t capacity;
} json_writer_t;

static void jw_init(json_writer_t* jw, char* buf, size_t cap)
{
    jw->buf = buf;
    jw->pos = 0;
    jw->capacity = cap;
    buf[0] = '\0';
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
        if (jw->pos >= jw->capacity) {
            jw->pos = jw->capacity - 1;
            jw->buf[jw->pos] = '\0';
        }
    }
}

/* Append a JSON key-value pair: "key": value */
static void jw_kv_str(json_writer_t* jw, const char* key, const char* val)
{
    jw_printf(jw, "\"%s\":\"%s\",", key, val);
}

static void jw_kv_uint(json_writer_t* jw, const char* key, uint32_t val)
{
    jw_printf(jw, "\"%s\":%u,", key, (unsigned)val);
}

static void jw_kv_u64(json_writer_t* jw, const char* key, uint64_t val)
{
    jw_printf(jw, "\"%s\":%llu,", key, (unsigned long long)val);
}

static void jw_kv_bool(json_writer_t* jw, const char* key, bool val)
{
    jw_printf(jw, "\"%s\":%s,", key, val ? "true" : "false");
}

__attribute__((unused))
static void jw_kv_double(json_writer_t* jw, const char* key, double val)
{
    jw_printf(jw, "\"%s\":%.6f,", key, val);
}

/* ---- Snapshot age helper ---- */
static uint64_t gnss_age_ms(const gnss_um980_t* gnss)
{
    if (gnss == NULL) return 0;
    uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000);
    if (gnss->snapshot.last_gga_time_ms > 0 && now_ms > gnss->snapshot.last_gga_time_ms) {
        return now_ms - gnss->snapshot.last_gga_time_ms;
    }
    return 0;
}

/* ---- Compute uptime in ms ---- */
static uint64_t get_uptime_ms(const remote_diag_t* diag)
{
    int64_t now = esp_timer_get_time();
    if (diag->boot_time_us == 0) return 0;
    int64_t delta_us = now - (int64_t)diag->boot_time_us;
    return (delta_us > 0) ? (uint64_t)(delta_us / 1000) : 0;
}

/* =================================================================
 * /status handler — comprehensive system status
 * ================================================================= */

static void build_status_json(remote_diag_t* diag, char* buf, size_t buf_size)
{
    json_writer_t jw;
    jw_init(&jw, buf, buf_size);

    jw_put(&jw, "{");

    /* ---- System ---- */
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
        esp_reset_reason_t reason = esp_reset_reason();
        const char* reason_str = "unknown";
        switch (reason) {
            case ESP_RST_POWERON:    reason_str = "poweron"; break;
            case ESP_RST_EXT:        reason_str = "external"; break;
            case ESP_RST_SW:         reason_str = "software"; break;
            case ESP_RST_PANIC:      reason_str = "panic"; break;
            case ESP_RST_INT_WDT:    reason_str = "int_wdt"; break;
            case ESP_RST_TASK_WDT:   reason_str = "task_wdt"; break;
            case ESP_RST_WDT:        reason_str = "wdt"; break;
            case ESP_RST_DEEPSLEEP:  reason_str = "deepsleep"; break;
            case ESP_RST_BROWNOUT:   reason_str = "brownout"; break;
            case ESP_RST_SDIO:       reason_str = "sdio"; break;
            default: break;
        }
        jw_kv_str(&jw, "reset_reason", reason_str);
    }

    /* Ethernet (using hal_eth_is_connected as best available in current codebase) */
    jw_kv_bool(&jw, "eth_link", hal_eth_is_connected());

    /* ---- Fast Loop ---- */
    {
        jw_kv_bool(&jw, "fast_alive", runtime_stats_get_worst() > 0 || runtime_stats_get_last() > 0);
        jw_kv_uint(&jw, "fast_last_us", runtime_stats_get_last());
        jw_kv_uint(&jw, "fast_worst_us", runtime_stats_get_worst());
        /* deadline_miss_count and hz_centi may not exist in current runtime_stats — report not_available */
        jw_kv_str(&jw, "fast_deadline_misses", "not_available");
        jw_kv_str(&jw, "fast_hz", "not_available");
    }

    /* ---- GNSS per receiver ---- */
    const gnss_um980_t* gnss_list[2] = {
        as_gnss(diag->primary_gnss),
        as_gnss(diag->secondary_gnss)
    };
    const char* gnss_names[2] = { "gnss1", "gnss2" };

    for (int g = 0; g < 2; g++) {
        const gnss_um980_t* gnss = gnss_list[g];
        const char* prefix = gnss_names[g];

        if (gnss != NULL) {
            jw_kv_uint(&jw, prefix, 1); /* marker: receiver present */
            jw_printf(&jw, "\"%s_uart_rx_bytes\":%u,", prefix, gnss->bytes_received);
            jw_printf(&jw, "\"%s_nmea_valid\":%u,", prefix, gnss->sentences_parsed);
            jw_printf(&jw, "\"%s_nmea_csum_fail\":%u,", prefix, gnss->checksum_errors);
            jw_printf(&jw, "\"%s_nmea_overflow\":%u,", prefix, gnss->overflow_errors);
            jw_printf(&jw, "\"%s_gga_count\":%u,", prefix, gnss->gga_count);
            jw_printf(&jw, "\"%s_rmc_count\":%u,", prefix, gnss->rmc_count);
            jw_printf(&jw, "\"%s_gst_count\":%u,", prefix, gnss->gst_count);
            jw_printf(&jw, "\"%s_last_gga_age_ms\":%llu,", prefix,
                      (unsigned long long)gnss_age_ms(gnss));
            jw_printf(&jw, "\"%s_snap_valid\":%s,", prefix,
                      gnss->snapshot.valid ? "true" : "false");
            jw_printf(&jw, "\"%s_snap_fresh\":%s,", prefix,
                      gnss->snapshot.fresh ? "true" : "false");
            jw_printf(&jw, "\"%s_fix\":\"%s\",", prefix,
                      fix_quality_str(gnss->snapshot.fix_quality));
            jw_printf(&jw, "\"%s_rtk\":\"%s\",", prefix,
                      rtk_status_str(gnss->snapshot.rtk_status));
            jw_printf(&jw, "\"%s_satellites\":%u,", prefix,
                      (unsigned)gnss->snapshot.satellites);
            jw_printf(&jw, "\"%s_hdop\":%.1f,", prefix, gnss->snapshot.hdop);
            jw_printf(&jw, "\"%s_invalid_reason\":\"%s\",", prefix,
                      status_reason_str(gnss->snapshot.status_reason));

            /* Lat/lon if valid */
            if (gnss->snapshot.valid) {
                jw_printf(&jw, "\"%s_lat\":%.7f,", prefix, gnss->snapshot.latitude);
                jw_printf(&jw, "\"%s_lon\":%.7f,", prefix, gnss->snapshot.longitude);
            } else {
                jw_printf(&jw, "\"%s_lat\":\"not_available\",", prefix);
                jw_printf(&jw, "\"%s_lon\":\"not_available\",", prefix);
            }
        } else {
            jw_kv_str(&jw, prefix, "not_available");
        }
    }

    /* ---- NTRIP ---- */
    {
        const ntrip_client_t* ntrip = as_ntrip(diag->ntrip);
        if (ntrip != NULL) {
            jw_kv_bool(&jw, "ntrip_started", ntrip->started);
            jw_kv_bool(&jw, "ntrip_config_valid", ntrip->config_valid);
            jw_kv_str(&jw, "ntrip_state", ntrip_state_str(ntrip->state));
            jw_kv_uint(&jw, "ntrip_reconnects", ntrip->reconnect_count);
            /* bytes_rx_total and last_rx_age_ms not in current struct */
            jw_kv_str(&jw, "ntrip_bytes_rx", "not_available");
            jw_kv_str(&jw, "ntrip_last_rx_age_ms", "not_available");
        } else {
            jw_kv_str(&jw, "ntrip", "not_available");
        }
    }

    /* ---- RTCM Router ---- */
    {
        const rtcm_router_t* router = as_rtcm_router(diag->rtcm_router);
        if (router != NULL) {
            const rtcm_stats_t* stats = rtcm_router_get_stats(router);
            uint32_t in_bytes = (stats != NULL) ? stats->bytes_in : 0;
            uint32_t out1_fwd = (router->output_count >= 1) ? router->outputs[0].bytes_forwarded : 0;
            uint32_t out2_fwd = (router->output_count >= 2) ? router->outputs[1].bytes_forwarded : 0;
            uint32_t drops = (stats != NULL) ? stats->bytes_dropped : 0;

            jw_kv_uint(&jw, "rtcm_in_bytes", in_bytes);
            jw_printf(&jw, "\"rtcm_uart1_tx_bytes\":%u,", out1_fwd);
            jw_printf(&jw, "\"rtcm_uart2_tx_bytes\":%u,", out2_fwd);
            jw_kv_uint(&jw, "rtcm_drops", drops);
            jw_kv_str(&jw, "rtcm_uart1_last_tx_age_ms", "not_available");
            jw_kv_str(&jw, "rtcm_uart2_last_tx_age_ms", "not_available");
        } else {
            jw_kv_str(&jw, "rtcm", "not_available");
        }
    }

    /* ---- AOG / PGN 214 ---- */
    {
        const aog_nav_app_t* app = as_nav_app(diag->nav_app);
        if (app != NULL) {
            jw_kv_uint(&jw, "pgn214_sent", app->pgn214_sent_count);
            jw_kv_uint(&jw, "aog_cycles", app->cycle_count);
            jw_kv_bool(&jw, "aog_position_valid", app->position_valid);
            jw_kv_bool(&jw, "aog_heading_valid", app->heading_valid);
            jw_kv_str(&jw, "pgn214_last_send_age_ms", "not_available");
            jw_kv_str(&jw, "pgn214_drops", "not_available");
        } else {
            jw_kv_str(&jw, "aog", "not_available");
        }
    }

    /* ---- UART stats per receiver ---- */
    {
        const transport_uart_t* uarts[2] = {
            as_uart(diag->primary_uart),
            as_uart(diag->secondary_uart)
        };
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

    /* ---- Health ---- */
    {
        runtime_health_state_t health = runtime_health_get();
        const char* health_str = "ok";
        switch (health) {
            case RUNTIME_HEALTH_WARN:  health_str = "warn"; break;
            case RUNTIME_HEALTH_ERROR: health_str = "error"; break;
            case RUNTIME_HEALTH_FATAL: health_str = "fatal"; break;
            default: break;
        }
        jw_kv_str(&jw, "health", health_str);
    }

    /* ---- UDP ---- */
    {
        const transport_udp_t* udp = as_udp(diag->udp);
        if (udp != NULL) {
            jw_kv_bool(&jw, "udp_bound", udp->bound);
            jw_kv_str(&jw, "udp_tx_packets", "not_available");
            jw_kv_str(&jw, "udp_tx_errors", "not_available");
        } else {
            jw_kv_str(&jw, "udp", "not_available");
        }
    }

    /* ---- Heading ---- */
    {
        const gnss_dual_heading_calc_t* hdg = as_heading(diag->heading);
        if (hdg != NULL) {
            jw_kv_bool(&jw, "heading_valid", hdg->heading.valid);
            jw_printf(&jw, "\"heading_deg\":%.1f,", hdg->heading.heading_deg);
            jw_kv_uint(&jw, "heading_calc_count", hdg->heading.calc_count);
        } else {
            jw_kv_str(&jw, "heading", "not_available");
        }
    }

    /* Remove trailing comma before closing brace */
    if (jw.pos > 1 && jw.buf[jw.pos - 1] == ',') {
        jw.buf[jw.pos - 1] = '\0';
        jw.pos--;
    }
    jw_put(&jw, "}");
}

/* =================================================================
 * /diag handler — lightweight diagnostic summary (plain text)
 * ================================================================= */

static void build_diag_text(remote_diag_t* diag, char* buf, size_t buf_size)
{
    int offset = 0;

    offset += snprintf(buf + offset, buf_size - offset,
        "=== AOG ESP Multiboard Remote Diag ===\n");

    /* System */
    offset += snprintf(buf + offset, buf_size - offset,
        "System: board=%s role=NAV mode=%s uptime=%llums reset=",
        board_name_str(), system_mode_str(),
        (unsigned long long)get_uptime_ms(diag));
    {
        esp_reset_reason_t r = esp_reset_reason();
        const char* rs = "unknown";
        switch (r) {
            case ESP_RST_POWERON:  rs = "poweron"; break;
            case ESP_RST_EXT:      rs = "ext"; break;
            case ESP_RST_SW:       rs = "sw"; break;
            case ESP_RST_PANIC:    rs = "panic"; break;
            default: break;
        }
        offset += snprintf(buf + offset, buf_size - offset, "%s", rs);
    }
    offset += snprintf(buf + offset, buf_size - offset,
        " eth=%s\n", hal_eth_is_connected() ? "up" : "down");

    /* Fast loop */
    offset += snprintf(buf + offset, buf_size - offset,
        "FastLoop: last_us=%lu worst_us=%lu alive=%s\n",
        (unsigned long)runtime_stats_get_last(), (unsigned long)runtime_stats_get_worst(),
        (runtime_stats_get_last() > 0) ? "yes" : "no");

    /* GNSS */
    const gnss_um980_t* gnss_arr[2] = {
        as_gnss(diag->primary_gnss),
        as_gnss(diag->secondary_gnss)
    };
    const char* labels[2] = { "Primary", "Secondary" };

    for (int g = 0; g < 2; g++) {
        const gnss_um980_t* gnss = gnss_arr[g];
        if (gnss != NULL) {
            offset += snprintf(buf + offset, buf_size - offset,
                "GNSS-%s: rx_bytes=%lu nmea_ok=%lu nmea_bad=%lu "
                "gga=%lu rmc=%lu gst=%lu "
                "valid=%s fresh=%s fix=%s rtk=%s sats=%u hdop=%.1f "
                "age=%llums reason=%s\n",
                labels[g],
                (unsigned long)gnss->bytes_received,
                (unsigned long)gnss->sentences_parsed,
                (unsigned long)gnss->checksum_errors,
                (unsigned long)gnss->gga_count,
                (unsigned long)gnss->rmc_count,
                (unsigned long)gnss->gst_count,
                gnss->snapshot.valid ? "yes" : "no",
                gnss->snapshot.fresh ? "yes" : "no",
                fix_quality_str(gnss->snapshot.fix_quality),
                rtk_status_str(gnss->snapshot.rtk_status),
                (unsigned)gnss->snapshot.satellites,
                gnss->snapshot.hdop,
                (unsigned long long)gnss_age_ms(gnss),
                status_reason_str(gnss->snapshot.status_reason));
        } else {
            offset += snprintf(buf + offset, buf_size - offset,
                "GNSS-%s: not_available\n", labels[g]);
        }
    }

    /* NTRIP */
    {
        const ntrip_client_t* ntrip = as_ntrip(diag->ntrip);
        if (ntrip != NULL) {
            offset += snprintf(buf + offset, buf_size - offset,
                "NTRIP: started=%s config=%s state=%s reconnects=%lu\n",
                ntrip->started ? "yes" : "no",
                ntrip->config_valid ? "yes" : "no",
                ntrip_state_str(ntrip->state),
                (unsigned long)ntrip->reconnect_count);
        } else {
            offset += snprintf(buf + offset, buf_size - offset,
                "NTRIP: not_available\n");
        }
    }

    /* RTCM Router */
    {
        const rtcm_router_t* router = as_rtcm_router(diag->rtcm_router);
        if (router != NULL) {
            const rtcm_stats_t* stats = rtcm_router_get_stats(router);
            uint32_t in_b = (stats != NULL) ? stats->bytes_in : 0;
            uint32_t drops = (stats != NULL) ? stats->bytes_dropped : 0;
            uint32_t o1 = (router->output_count >= 1) ? router->outputs[0].bytes_forwarded : 0;
            uint32_t o2 = (router->output_count >= 2) ? router->outputs[1].bytes_forwarded : 0;
            offset += snprintf(buf + offset, buf_size - offset,
                "RTCM: in=%lu uart1_tx=%lu uart2_tx=%lu drops=%lu\n",
                (unsigned long)in_b, (unsigned long)o1, (unsigned long)o2, (unsigned long)drops);
        } else {
            offset += snprintf(buf + offset, buf_size - offset,
                "RTCM: not_available\n");
        }
    }

    /* AOG */
    {
        const aog_nav_app_t* app = as_nav_app(diag->nav_app);
        if (app != NULL) {
            offset += snprintf(buf + offset, buf_size - offset,
                "AOG: pgn214_sent=%lu cycles=%lu pos_valid=%s hdg_valid=%s\n",
                (unsigned long)app->pgn214_sent_count, (unsigned long)app->cycle_count,
                app->position_valid ? "yes" : "no",
                app->heading_valid ? "yes" : "no");
        } else {
            offset += snprintf(buf + offset, buf_size - offset,
                "AOG: not_available\n");
        }
    }

    /* Heading */
    {
        const gnss_dual_heading_calc_t* hdg = as_heading(diag->heading);
        if (hdg != NULL) {
            offset += snprintf(buf + offset, buf_size - offset,
                "HDG: valid=%s deg=%.1f calcs=%lu\n",
                hdg->heading.valid ? "yes" : "no",
                hdg->heading.heading_deg, (unsigned long)hdg->heading.calc_count);
        } else {
            offset += snprintf(buf + offset, buf_size - offset,
                "HDG: not_available\n");
        }
    }
}

/* =================================================================
 * HTTP URI Handlers
 * ================================================================= */

static esp_err_t status_handler(httpd_req_t* req)
{
    if (s_diag_instance == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char json_buf[JSON_BUF_SIZE];
    build_status_json(s_diag_instance, json_buf, sizeof(json_buf));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_buf, strlen(json_buf));
    return ESP_OK;
}

static esp_err_t diag_handler(httpd_req_t* req)
{
    if (s_diag_instance == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char text_buf[2048];
    build_diag_text(s_diag_instance, text_buf, sizeof(text_buf));

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, text_buf, strlen(text_buf));
    return ESP_OK;
}

static esp_err_t version_handler(httpd_req_t* req)
{
    const char* version_text =
        "aog_esp_multiboard\n"
        "framework: esp-idf (platformio)\n"
        "task: NAV-REMOTE-DIAG-001\n";

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, version_text, strlen(version_text));
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
    diag->http_started = false;

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
    diag->primary_uart   = primary_uart;
    diag->secondary_uart = secondary_uart;
    diag->primary_gnss   = primary_gnss;
    diag->secondary_gnss = secondary_gnss;
    diag->heading        = heading;
    diag->nav_app        = nav_app;
    diag->udp            = udp;
    diag->ntrip          = ntrip;
    diag->rtcm_router    = rtcm_router;
}

void remote_diag_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    remote_diag_t* diag = (remote_diag_t*)comp;
    if (diag == NULL) return;

    /* Record boot time on first call */
    if (diag->boot_time_us == 0) {
        diag->boot_time_us = esp_timer_get_time();
    }

    /* Start HTTP server once (never stop) */
    if (!diag->http_started) {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = REMOTE_DIAG_HTTP_PORT;
        config.max_uri_handlers = REMOTE_DIAG_MAX_URI_HANDLERS;
        /* Use minimal stack — our handlers are lightweight */
        config.stack_size = 4096;

        httpd_handle_t server = NULL;
        esp_err_t err = httpd_start(&server, &config);
        if (err == ESP_OK) {
            /* Register URI handlers */
            httpd_uri_t status_uri = {
                .uri      = "/status",
                .method   = HTTP_GET,
                .handler  = status_handler,
                .user_ctx = NULL
            };
            httpd_uri_t diag_uri = {
                .uri      = "/diag",
                .method   = HTTP_GET,
                .handler  = diag_handler,
                .user_ctx = NULL
            };
            httpd_uri_t version_uri = {
                .uri      = "/version",
                .method   = HTTP_GET,
                .handler  = version_handler,
                .user_ctx = NULL
            };

            httpd_register_uri_handler(server, &status_uri);
            httpd_register_uri_handler(server, &diag_uri);
            httpd_register_uri_handler(server, &version_uri);

            diag->http_started = true;
            ESP_LOGI(TAG, "HTTP server started on port %d", REMOTE_DIAG_HTTP_PORT);
        } else {
            ESP_LOGE(TAG, "Failed to start HTTP server: 0x%x (%s)",
                     (unsigned)err, esp_err_to_name(err));
        }
    }

    (void)timestamp_us;
}
