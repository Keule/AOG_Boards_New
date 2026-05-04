/* ========================================================================
 * hw_runtime_diag.c — Hardware Runtime Diagnostics
 *   NAV-HW-RUNTIME-DIAG-001 + NAV-ETH-BRINGUP-001-R2 + NAV-RTCM-PIPELINE-001
 *
 * Periodic health reporter: fast-loop stats, GNSS UART RX counters,
 * GNSS snapshot validity, Ethernet/UDP status, NTRIP status, PGN214 TX,
 * RTCM pipeline diagnostics (router in/out/drops, UART TX bytes/drops).
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

#pragma GCC diagnostic ignored "-Wformat"

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
#include "rtcm_router.h"
#include "transport_tcp.h"
#include "nmea_parser.h"

/* static const char* TAG unused — logs use inline string literals */

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

    /* Use valid GGA time as primary age indicator */
    uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000);
    uint64_t ref = gnss->snapshot.last_valid_gga_time_ms > 0
                   ? gnss->snapshot.last_valid_gga_time_ms
                   : gnss->snapshot.last_gga_time_ms;
    if (ref > 0) {
        uint64_t age = now_ms - ref;
        return (age > 0xFFFFFFFFUL) ? 0xFFFFFFFFUL : (uint32_t)age;
    }
    return 0;
}

/* ---- Snapshot last valid GGA/RMC age ---- */
static uint32_t snapshot_valid_gga_age_ms(const gnss_um980_t* gnss)
{
    if (gnss == NULL || gnss->snapshot.last_valid_gga_time_ms == 0) return 0xFFFFFFFFUL;
    uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000);
    uint64_t age = now_ms - gnss->snapshot.last_valid_gga_time_ms;
    return (age > 0xFFFFFFFFUL) ? 0xFFFFFFFFUL : (uint32_t)age;
}

static uint32_t snapshot_valid_rmc_age_ms(const gnss_um980_t* gnss)
{
    if (gnss == NULL || gnss->snapshot.last_valid_rmc_time_ms == 0) return 0xFFFFFFFFUL;
    uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000);
    uint64_t age = now_ms - gnss->snapshot.last_valid_rmc_time_ms;
    return (age > 0xFFFFFFFFUL) ? 0xFFFFFFFFUL : (uint32_t)age;
}

/* ---- Determine last received sentence type from counters ---- */
static const char* last_sentence_type(const gnss_um980_t* gnss)
{
    if (gnss == NULL || gnss->sentences_parsed == 0) return "none";
    /* Use gga as default since it's the most frequent */
    return "GGA";
}

/* saturated_sub — keep for future use */
static uint32_t saturated_sub(uint32_t a, uint32_t b) __attribute__((unused));
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
    diag->rtcm_router = NULL;
}

void hw_runtime_diag_set_sources(hw_runtime_diag_t* diag,
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

    /* ---- GNSS UART RX Counters (NAV-GNSS-NMEA-CORRUPTION-001) ---- */
    {
        const transport_uart_t* p_uart = (const transport_uart_t*)diag->primary_uart;
        const gnss_um980_t*     p_gnss = (const gnss_um980_t*)diag->primary_gnss;

        if (p_uart != NULL && p_gnss != NULL) {
            ESP_LOGI("HW_DIAG",
                     "GNSS_RX: primary uart=1 bytes=%u lines=%u "
                     "gga=%u rmc=%u gst=%u gsa=%u gsv=%u unk=%u "
                     "csum_bad=%u binary_reject=%u garbage=%u "
                     "vGGA_age=%u vRMC_age=%u last=%s",
                     p_gnss->bytes_received,
                     p_gnss->sentences_parsed,
                     p_gnss->gga_count,
                     p_gnss->rmc_count,
                     p_gnss->gst_count,
                     p_gnss->gsa_count,
                     p_gnss->gsv_count,
                     p_gnss->unknown_prefix_count,
                     p_gnss->checksum_errors,
                     p_gnss->binary_rejects,
                     p_gnss->garbage_discarded,
                     snapshot_valid_gga_age_ms(p_gnss),
                     snapshot_valid_rmc_age_ms(p_gnss),
                     last_sentence_type(p_gnss));
        }

        const transport_uart_t* s_uart = (const transport_uart_t*)diag->secondary_uart;
        const gnss_um980_t*     s_gnss = (const gnss_um980_t*)diag->secondary_gnss;

        if (s_uart != NULL && s_gnss != NULL) {
            ESP_LOGI("HW_DIAG",
                     "GNSS_RX: secondary uart=2 bytes=%u lines=%u "
                     "gga=%u rmc=%u gst=%u gsa=%u gsv=%u unk=%u "
                     "csum_bad=%u binary_reject=%u garbage=%u "
                     "vGGA_age=%u vRMC_age=%u last=%s",
                     s_gnss->bytes_received,
                     s_gnss->sentences_parsed,
                     s_gnss->gga_count,
                     s_gnss->rmc_count,
                     s_gnss->gst_count,
                     s_gnss->gsa_count,
                     s_gnss->gsv_count,
                     s_gnss->unknown_prefix_count,
                     s_gnss->checksum_errors,
                     s_gnss->binary_rejects,
                     s_gnss->garbage_discarded,
                     snapshot_valid_gga_age_ms(s_gnss),
                     snapshot_valid_rmc_age_ms(s_gnss),
                     last_sentence_type(s_gnss));
        }
    }

    /* ---- NAV-GNSS-NMEA-CORRUPTION-001: GNSS UART Buffer Diagnostics (WP-D) ---- */
    {
        const transport_uart_t* p_uart = (const transport_uart_t*)diag->primary_uart;
        const transport_uart_t* s_uart = (const transport_uart_t*)diag->secondary_uart;
        const rtcm_router_t*    router  = (const rtcm_router_t*)diag->rtcm_router;

        if (p_uart != NULL) {
            const transport_uart_stats_t* us = transport_uart_get_stats(p_uart);
            uint32_t rx_buf_used = (uint32_t)byte_ring_buffer_available(&p_uart->rx_buffer);
            uint32_t rx_buf_size = (uint32_t)p_uart->rx_buffer.capacity;
            uint32_t tx_bytes = (us != NULL) ? us->tx_bytes_out : 0;
            uint32_t tx_drops = (router != NULL && router->output_count >= 1)
                                ? router->outputs[0].bytes_dropped : 0;
            uint32_t rx_over = (us != NULL) ? us->rx_overflow_count : 0;

            ESP_LOGI("HW_DIAG",
                     "GNSS_UART: primary rx_bytes=%u rx_overruns=%u "
                     "rx_ring=%u/%u tx_rtcm_bytes=%u tx_drops=%u",
                     (us != NULL) ? us->rx_bytes_in : 0,
                     (unsigned)rx_over,
                     (unsigned)rx_buf_used, (unsigned)rx_buf_size,
                     (unsigned)tx_bytes, (unsigned)tx_drops);
        }

        if (s_uart != NULL) {
            const transport_uart_stats_t* us = transport_uart_get_stats(s_uart);
            uint32_t rx_buf_used = (uint32_t)byte_ring_buffer_available(&s_uart->rx_buffer);
            uint32_t rx_buf_size = (uint32_t)s_uart->rx_buffer.capacity;
            uint32_t tx_bytes = (us != NULL) ? us->tx_bytes_out : 0;
            uint32_t tx_drops = (router != NULL && router->output_count >= 2)
                                ? router->outputs[1].bytes_dropped : 0;
            uint32_t rx_over = (us != NULL) ? us->rx_overflow_count : 0;

            ESP_LOGI("HW_DIAG",
                     "GNSS_UART: secondary rx_bytes=%u rx_overruns=%u "
                     "rx_ring=%u/%u tx_rtcm_bytes=%u tx_drops=%u",
                     (us != NULL) ? us->rx_bytes_in : 0,
                     (unsigned)rx_over,
                     (unsigned)rx_buf_used, (unsigned)rx_buf_size,
                     (unsigned)tx_bytes, (unsigned)tx_drops);
        }
    }

    /* ---- NAV-GNSS-NMEA-CORRUPTION-001: GNSS Bad Line Diagnostic (WP-A/B, rate-limited) ---- */
    {
        const gnss_um980_t* p_gnss = (const gnss_um980_t*)diag->primary_gnss;
        const gnss_um980_t* s_gnss = (const gnss_um980_t*)diag->secondary_gnss;

        /* Rate-limit: only print once per diagnostic cycle (5s) */
        static uint32_t s_bad_line_print_mask = 0;
        uint32_t print_mask = 0;

        if (p_gnss != NULL && p_gnss->checksum_errors > 0) {
            print_mask |= 0x01;
        }
        if (s_gnss != NULL && s_gnss->checksum_errors > 0) {
            print_mask |= 0x02;
        }

        if (p_gnss != NULL && (print_mask & 0x01) && !(s_bad_line_print_mask & 0x01)) {
            const nmea_parser_t* np = &p_gnss->nmea_parser;
            ESP_LOGW("HW_DIAG",
                     "GNSS_BAD_CSUM: primary parsed=0x%02x computed=0x%02x "
                     "reason=%s len=%u",
                     (unsigned)np->bad_parsed_csum,
                     (unsigned)np->bad_computed_csum,
                     (np->bad_line_reason == 0) ? "binary" :
                     (np->bad_line_reason == 1) ? "checksum" :
                     (np->bad_line_reason == 2) ? "overflow" :
                     (np->bad_line_reason == 3) ? "malformed_csum" : "unknown",
                     (unsigned)np->bad_line_len);
        }
        if (s_gnss != NULL && (print_mask & 0x02) && !(s_bad_line_print_mask & 0x02)) {
            const nmea_parser_t* np = &s_gnss->nmea_parser;
            ESP_LOGW("HW_DIAG",
                     "GNSS_BAD_CSUM: secondary parsed=0x%02x computed=0x%02x "
                     "reason=%s len=%u",
                     (unsigned)np->bad_parsed_csum,
                     (unsigned)np->bad_computed_csum,
                     (np->bad_line_reason == 0) ? "binary" :
                     (np->bad_line_reason == 1) ? "checksum" :
                     (np->bad_line_reason == 2) ? "overflow" :
                     (np->bad_line_reason == 3) ? "malformed_csum" : "unknown",
                     (unsigned)np->bad_line_len);
        }

        /* Toggle print mask: print on odd cycles, suppress on even */
        s_bad_line_print_mask = print_mask;
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
                     "AOG_NAV: pgn214_tx=%lu cycle=%lu state=%s",
                     (unsigned long)app->pgn214_send_count,
                     (unsigned long)app->cycle_count,
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
                    /* Show rx_bytes + tcp_drops (NAV-RTCM-PIPELINE-001) */
                    uint32_t rx = 0;
                    uint32_t drops = 0;
                    if (ntrip->transport != NULL) {
                        rx = transport_tcp_get_total_rx(ntrip->transport);
                        drops = transport_tcp_get_rx_drops(ntrip->transport);
                    }
                    ESP_LOGI("HW_DIAG",
                             "NTRIP: eth_ready=%u config_ready=%u state=%s "
                             "rx_bytes=%u reconnects=%u tcp_drops=%u",
                             (unsigned)ntrip_eth,
                             (unsigned)config_ready,
                             display_state,
                             (unsigned)rx,
                             (unsigned)ntrip->reconnect_count,
                             (unsigned)drops);
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

    /* ---- NAV-RTCM-PIPELINE-001: RTCM Router Diagnostics ---- */
    {
        const rtcm_router_t* router = (const rtcm_router_t*)diag->rtcm_router;
        if (router != NULL) {
            const rtcm_stats_t* stats = rtcm_router_get_stats(router);

            /* Per-output counters (max 2 outputs) */
            uint32_t out1_fwd = 0, out1_drop = 0;
            uint32_t out2_fwd = 0, out2_drop = 0;
            (void)out1_drop; (void)out2_drop;

            if (router->output_count >= 1) {
                out1_fwd  = router->outputs[0].bytes_forwarded;
                out1_drop = router->outputs[0].bytes_dropped;
            }
            if (router->output_count >= 2) {
                out2_fwd  = router->outputs[1].bytes_forwarded;
                out2_drop = router->outputs[1].bytes_dropped;
            }

            uint32_t total_drops = (stats != NULL) ? stats->bytes_dropped : 0;
            uint32_t in_bytes = (stats != NULL) ? stats->bytes_in : 0;

            ESP_LOGI("HW_DIAG",
                     "RTCM_ROUTER: in_bytes=%u out1_bytes=%u out2_bytes=%u drops=%u",
                     (unsigned)in_bytes,
                     (unsigned)out1_fwd,
                     (unsigned)out2_fwd,
                     (unsigned)total_drops);
        }
    }

    /* ---- NAV-RTCM-PIPELINE-001: GNSS RTCM TX (UART) Diagnostics ---- */
    {
        const rtcm_router_t* router = (const rtcm_router_t*)diag->rtcm_router;
        const transport_uart_t* p_uart = (const transport_uart_t*)diag->primary_uart;
        const transport_uart_t* s_uart = (const transport_uart_t*)diag->secondary_uart;

        /* Primary UART TX */
        if (p_uart != NULL && router != NULL && router->output_count >= 1) {
            const transport_uart_stats_t* ustats = transport_uart_get_stats(p_uart);
            uint32_t tx_bytes = (ustats != NULL) ? ustats->tx_bytes_out : 0;
            uint32_t drops = router->outputs[0].bytes_dropped;

            ESP_LOGI("HW_DIAG",
                     "GNSS_RTCM_TX: primary uart=1 tx_bytes=%u drops=%u",
                     (unsigned)tx_bytes,
                     (unsigned)drops);
        }

        /* Secondary UART TX */
        if (s_uart != NULL && router != NULL && router->output_count >= 2) {
            const transport_uart_stats_t* ustats = transport_uart_get_stats(s_uart);
            uint32_t tx_bytes = (ustats != NULL) ? ustats->tx_bytes_out : 0;
            uint32_t drops = router->outputs[1].bytes_dropped;

            ESP_LOGI("HW_DIAG",
                     "GNSS_RTCM_TX: secondary uart=2 tx_bytes=%u drops=%u",
                     (unsigned)tx_bytes,
                     (unsigned)drops);
        }
    }
}
