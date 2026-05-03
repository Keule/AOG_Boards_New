/* ========================================================================
 * hw_runtime_diag.c — Hardware Runtime Diagnostics
 *   NAV-HW-RUNTIME-DIAG-001 + NAV-ETH-BRINGUP-001-R2
 *
 * Periodic health reporter: fast-loop stats, GNSS UART RX counters,
 * GNSS snapshot validity, Ethernet/UDP status, NTRIP status, PGN214 TX.
 *
 * Runs in SERVICE_GROUP_DIAGNOSTICS on Core 0 at ~100ms period.
 * Actual output at 0.2 Hz (every 5 seconds) to avoid log spam.
 *
 * Changes in NAV-ETH-BRINGUP-001-R2:
 *   WP-A/C: UDP health reads real transport_udp_get_status()
 *   WP-B:   Real UDP reference (no cast from nav_app)
 *   WP-D:   GNSS checksum_ok saturated (no uint32 underflow)
 *   WP-E:   Fast-loop: processing_us, remaining_us, real hz, period stats
 *   WP-H:   NTRIP status diagnostics (config_ready, state, no secrets)
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
#include "hal_eth.h"
#include "transport_udp.h"
#include "ntrip_client.h"

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

/* ---- WP-D: Saturated subtraction for checksum_ok ---- */
static uint32_t saturated_sub(uint32_t a, uint32_t b)
{
    return (a >= b) ? (a - b) : 0;
}

/* ---- NTRIP state name ---- */
static const char* ntrip_state_str(ntrip_state_t state)
{
    switch (state) {
        case NTRIP_STATE_IDLE:          return "disabled";
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
    diag->udp = NULL;
    diag->ntrip = NULL;
}

void hw_runtime_diag_set_sources(hw_runtime_diag_t* diag,
                                  const void* primary_uart,
                                  const void* secondary_uart,
                                  const void* primary_gnss,
                                  const void* secondary_gnss,
                                  const void* heading,
                                  const void* nav_app,
                                  const void* udp,
                                  const void* ntrip)
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

    /* ---- WP-E: Fast-Loop Health (precise) ---- */
    {
        uint32_t hz_centi = runtime_stats_get_hz_centi();
        uint32_t report_cycles = runtime_stats_get_report_cycles();
        uint64_t total_cycles = runtime_stats_get_total_cycles();
        uint32_t missed = runtime_stats_get_deadline_miss_count();
        uint32_t processing_us = runtime_stats_get_processing_us();
        uint32_t remaining_us = runtime_stats_get_remaining_us();
        uint32_t period_avg_us = runtime_stats_get_period_avg_us();
        uint32_t period_max_us = runtime_stats_get_period_max_us();

        /* Format hz as integer or 1-decimal: e.g. 9980 -> "99.8" */
        uint32_t hz_int = hz_centi / 100;
        uint32_t hz_frac = hz_centi % 100;

        if (hz_frac == 0) {
            ESP_LOGI("HW_DIAG",
                     "RUNTIME_FAST: hz=%u report_cycles=%u total_cycles=%llu "
                     "missed=%u processing_us=%u remaining_us=%u "
                     "period_avg_us=%u period_max_us=%u",
                     hz_int, report_cycles,
                     (unsigned long long)total_cycles,
                     missed, processing_us, remaining_us,
                     period_avg_us, period_max_us);
        } else {
            ESP_LOGI("HW_DIAG",
                     "RUNTIME_FAST: hz=%u.%02u report_cycles=%u total_cycles=%llu "
                     "missed=%u processing_us=%u remaining_us=%u "
                     "period_avg_us=%u period_max_us=%u",
                     hz_int, hz_frac, report_cycles,
                     (unsigned long long)total_cycles,
                     missed, processing_us, remaining_us,
                     period_avg_us, period_max_us);
        }

        /* Reset report window for next interval */
        runtime_stats_reset_report_window((int64_t)timestamp_us);
    }

    /* ---- WP-B: GNSS UART RX Counters (no underflow) ---- */
    {
        const transport_uart_t* p_uart = (const transport_uart_t*)diag->primary_uart;
        const gnss_um980_t*     p_gnss = (const gnss_um980_t*)diag->primary_gnss;

        if (p_uart != NULL && p_gnss != NULL) {
            ESP_LOGI("HW_DIAG",
                     "GNSS_RX: primary uart=1 bytes=%u lines=%u "
                     "checksum_ok=%u checksum_bad=%u last=%s",
                     p_gnss->bytes_received,
                     p_gnss->sentences_parsed,
                     saturated_sub(p_gnss->sentences_parsed, p_gnss->checksum_errors),
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
                     saturated_sub(s_gnss->sentences_parsed, s_gnss->checksum_errors),
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

    /* ---- WP-A/C: Ethernet/UDP Status (real, no more STUB/fake) ---- */
    {
        /* ---- Ethernet: real status from hal_eth ---- */
        const hal_eth_status_t* eth_st = hal_eth_get_status();
        bool eth_initialized = (eth_st != NULL && eth_st->initialized);

        if (eth_initialized) {
            if (eth_st->got_ip) {
                ESP_LOGI("HW_DIAG",
                         "ETH: driver=RTL8201/RMII link=%s ip=%u.%u.%u.%u",
                         eth_st->link_up ? "up" : "down",
                         eth_st->ip_addr[0], eth_st->ip_addr[1],
                         eth_st->ip_addr[2], eth_st->ip_addr[3]);
            } else if (eth_st->link_up) {
                ESP_LOGI("HW_DIAG",
                         "ETH: driver=RTL8201/RMII link=up ip=none (DHCP pending)");
            } else {
                ESP_LOGI("HW_DIAG",
                         "ETH: driver=RTL8201/RMII link=down ip=none");
            }
        } else {
            ESP_LOGI("HW_DIAG", "ETH: not_initialized");
        }

        /* ---- UDP: real status from transport_udp ---- */
        const transport_udp_t* udp = (const transport_udp_t*)diag->udp;
        if (udp != NULL) {
            const transport_udp_status_t* st = transport_udp_get_status(udp);

            if (st->socket_open && st->bound) {
                /* WP-C: Socket ready — distinguish from peer presence */
                uint8_t* ip_bytes = (uint8_t*)&udp->remote_ip;
                uint32_t peer_seen = (st->rx_packets > 0) ? 1 : 0;

                ESP_LOGI("HW_DIAG",
                         "UDP: ready rx=ok tx=ok local_port=%u "
                         "target=%u.%u.%u.%u:%u peer_seen=%u "
                         "tx_pkts=%u rx_pkts=%u",
                         (unsigned)udp->local_port,
                         ip_bytes[3], ip_bytes[2], ip_bytes[1], ip_bytes[0],
                         (unsigned)udp->remote_port,
                         peer_seen,
                         st->tx_packets, st->rx_packets);
            } else if (eth_initialized && eth_st->got_ip) {
                /* ETH has IP but socket not yet open — transient state */
                ESP_LOGI("HW_DIAG",
                         "UDP: eth_ready waiting_for_socket (has_ip=%u socket_open=%u bound=%u last_err=%d)",
                         (unsigned)st->has_ip,
                         (unsigned)st->socket_open,
                         (unsigned)st->bound,
                         st->last_error);
            } else if (eth_initialized && !eth_st->got_ip) {
                ESP_LOGI("HW_DIAG", "UDP: waiting_for_ip");
            } else {
                ESP_LOGI("HW_DIAG", "UDP: waiting_for_eth");
            }
        } else {
            /* No UDP reference wired — fallback to ETH status only */
            if (eth_initialized && eth_st->got_ip) {
                ESP_LOGI("HW_DIAG", "UDP: eth_ready (no_udp_ref)");
            } else if (eth_initialized) {
                ESP_LOGI("HW_DIAG", "UDP: waiting_for_ip (no_udp_ref)");
            } else {
                ESP_LOGI("HW_DIAG", "UDP: waiting_for_eth (no_udp_ref)");
            }
        }
    }

    /* ---- PGN214 TX counter from AOG nav app ---- */
    {
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

    /* ---- WP-H + NAV-NTRIP-NONBLOCKING-001 WP-D: NTRIP Status Diagnostics ---- */
    {
        const hal_eth_status_t* eth_st = hal_eth_get_status();
        bool ntrip_eth = (eth_st != NULL && eth_st->initialized && eth_st->got_ip);

        const ntrip_client_t* ntrip = (const ntrip_client_t*)diag->ntrip;
        if (ntrip != NULL) {
            bool config_ready = ntrip->config_valid;
            ntrip_state_t state = ntrip->state;
            bool started = ntrip->started;

            /* Determine display state with sub-reason */
            const char* display_state;
            const char* reason = "";

            if (!config_ready && !started) {
                display_state = "disabled";
                reason = "empty_config";
            } else if (!ntrip_eth) {
                display_state = "waiting";
                reason = "no_eth_ip";
            } else if (started) {
                display_state = ntrip_state_str(state);
                /* Add TCP sub-state for connecting state */
                if (state == NTRIP_STATE_CONNECTING && ntrip->transport != NULL) {
                    reason = transport_tcp_sub_state_name(
                        transport_tcp_get_sub_state(ntrip->transport));
                } else if (state == NTRIP_STATE_ERROR) {
                    reason = ntrip_client_error_str(ntrip->last_error);
                } else if (state == NTRIP_STATE_CONNECTED) {
                    /* Show rx_bytes */
                    uint32_t rx = ntrip_client_rtcm_available(ntrip);
                    if (ntrip->transport != NULL) {
                        rx = transport_tcp_get_total_rx(ntrip->transport);
                    }
                    ESP_LOGI("HW_DIAG",
                             "NTRIP: eth_ready=%u config_ready=%u state=%s "
                             "rx_bytes=%u reconnects=%u",
                             (unsigned)ntrip_eth,
                             (unsigned)config_ready,
                             display_state,
                             (unsigned)rx,
                             (unsigned)ntrip->reconnect_count);
                    goto ntrip_cfg_log;
                } else if (state == NTRIP_STATE_RETRY_WAIT) {
                    reason = "backoff";
                }
            } else {
                display_state = "ready";
                reason = "";
            }

            ESP_LOGI("HW_DIAG",
                     "NTRIP: eth_ready=%u config_ready=%u state=%s%s%s",
                     (unsigned)ntrip_eth,
                     (unsigned)config_ready,
                     display_state,
                     reason[0] ? " reason=" : "",
                     reason);
        } else {
            ESP_LOGI("HW_DIAG", "NTRIP: eth_ready=%u config_ready=0 state=disabled reason=no_ntrip_ref",
                     (unsigned)ntrip_eth);
        }

ntrip_cfg_log:
        if (ntrip != NULL) {
            /* WP-H: Config fields — NEVER log password */
            ESP_LOGI("HW_DIAG",
                     "NTRIP_CFG: host=%s port=%u mount=%s user=%s password=%s",
                     ntrip->config.host[0] ? "set" : "empty",
                     (unsigned)ntrip->config.port,
                     ntrip->config.mountpoint[0] ? "set" : "empty",
                     ntrip->config.username[0] ? "set" : "empty",
                     ntrip->config.password[0] ? "set" : "empty");
        }
    }
}
