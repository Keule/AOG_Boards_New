#include "nav_health.h"
#include <string.h>

/* ========================================================================
 * nav_health.c — NAV Health Snapshot Collector (NAV-DIAG-001 Teil 1)
 *
 * Types are resolved via nav_health.h which includes the real component
 * headers. No additional includes needed for type access.
 * ======================================================================== */

/* ---- Init ---- */

void nav_health_collector_init(nav_health_collector_t* coll)
{
    if (coll == NULL) { return; }
    memset(coll, 0, sizeof(nav_health_collector_t));
}

/* ---- Setters ---- */

void nav_health_collector_set_uart_primary(nav_health_collector_t* coll,
                                            transport_uart_t* uart)
{
    if (coll) { coll->uart_primary = uart; }
}

void nav_health_collector_set_uart_secondary(nav_health_collector_t* coll,
                                              transport_uart_t* uart)
{
    if (coll) { coll->uart_secondary = uart; }
}

void nav_health_collector_set_gnss_primary(nav_health_collector_t* coll,
                                            gnss_um980_t* gnss)
{
    if (coll) { coll->gnss_primary = gnss; }
}

void nav_health_collector_set_gnss_secondary(nav_health_collector_t* coll,
                                              gnss_um980_t* gnss)
{
    if (coll) { coll->gnss_secondary = gnss; }
}

void nav_health_collector_set_heading(nav_health_collector_t* coll,
                                       gnss_dual_heading_calc_t* heading)
{
    if (coll) { coll->heading = heading; }
}

void nav_health_collector_set_ntrip(nav_health_collector_t* coll,
                                     ntrip_client_t* ntrip)
{
    if (coll) { coll->ntrip = ntrip; }
}

void nav_health_collector_set_rtcm_router(nav_health_collector_t* coll,
                                           rtcm_router_t* router)
{
    if (coll) { coll->rtcm_router = router; }
}

void nav_health_collector_set_aog_nav_app(nav_health_collector_t* coll,
                                           aog_nav_app_t* app)
{
    if (coll) { coll->aog_nav_app = app; }
}

void nav_health_collector_set_tcp(nav_health_collector_t* coll,
                                   transport_tcp_t* tcp)
{
    if (coll) { coll->tcp = tcp; }
}

void nav_health_collector_set_eth_link(nav_health_collector_t* coll,
                                         bool link_up)
{
    if (coll) { coll->eth_link_up = link_up; }
}

/* ---- Error recording ---- */

void nav_health_record_error(nav_health_collector_t* coll,
                              nav_error_module_t module, uint32_t code)
{
    if (coll == NULL) { return; }
    coll->total_errors++;
    coll->last_error_module = (uint32_t)module;
    coll->last_error_code = code;
}

/* ---- UART field extraction ---- */

static void extract_uart_fields(const transport_uart_t* uart,
                                 uint32_t* rx_bytes, uint32_t* tx_bytes,
                                 uint32_t* rx_overflows, uint32_t* tx_errors)
{
    if (uart == NULL) {
        *rx_bytes = *tx_bytes = *rx_overflows = *tx_errors = 0;
        return;
    }
    *rx_bytes     = uart->stats.rx_bytes_in;
    *tx_bytes     = uart->stats.tx_bytes_out;
    *rx_overflows = uart->stats.rx_overflow_count;
    *tx_errors    = uart->stats.tx_errors;
}

/* ---- GNSS field extraction ---- */

static void extract_gnss_fields(const gnss_um980_t* gnss,
                                 bool* valid, bool* fresh,
                                 uint32_t* sentences, uint32_t* cksum_err,
                                 uint32_t* timeout_events, uint32_t* bytes)
{
    if (gnss == NULL) {
        *valid = *fresh = false;
        *sentences = *cksum_err = *timeout_events = *bytes = 0;
        return;
    }
    *valid          = gnss->snapshot.valid;
    *fresh          = gnss->snapshot.fresh;
    *sentences      = gnss->sentences_parsed;
    *cksum_err      = gnss->checksum_errors;
    *timeout_events = gnss->timeout_events;
    *bytes          = gnss->bytes_received;
}

/* ---- NTRIP state mapping ---- */

static nav_ntrip_state_t map_ntrip_state(ntrip_state_t raw)
{
    switch (raw) {
        case NTRIP_STATE_IDLE:           return NAV_NTRIP_STATE_IDLE;
        case NTRIP_STATE_CONNECTING:     return NAV_NTRIP_STATE_CONNECTING;
        case NTRIP_STATE_BUILD_REQUEST:  return NAV_NTRIP_STATE_BUILD_REQUEST;
        case NTRIP_STATE_SEND_REQUEST:   return NAV_NTRIP_STATE_SEND_REQUEST;
        case NTRIP_STATE_WAIT_RESPONSE:  return NAV_NTRIP_STATE_WAIT_RESPONSE;
        case NTRIP_STATE_CONNECTED:      return NAV_NTRIP_STATE_CONNECTED;
        case NTRIP_STATE_ERROR:          return NAV_NTRIP_STATE_ERROR;
        case NTRIP_STATE_RETRY_WAIT:     return NAV_NTRIP_STATE_RETRY_WAIT;
        default:                         return NAV_NTRIP_STATE_UNKNOWN;
    }
}

/* ---- AOG output state mapping ---- */

static nav_aog_state_t map_aog_state(aog_output_state_t raw)
{
    switch (raw) {
        case AOG_OUTPUT_INIT:             return NAV_AOG_STATE_INIT;
        case AOG_OUTPUT_OK:               return NAV_AOG_STATE_OK;
        case AOG_OUTPUT_GNSS_INVALID:     return NAV_AOG_STATE_GNSS_INVALID;
        case AOG_OUTPUT_GNSS_STALE:       return NAV_AOG_STATE_GNSS_STALE;
        case AOG_OUTPUT_HEADING_INVALID:  return NAV_AOG_STATE_HEADING_INVALID;
        case AOG_OUTPUT_HEADING_STALE:    return NAV_AOG_STATE_HEADING_STALE;
        case AOG_OUTPUT_HEADING_LOST:     return NAV_AOG_STATE_HEADING_LOST;
        case AOG_OUTPUT_SUPPRESSED:       return NAV_AOG_STATE_SUPPRESSED;
        default:                          return NAV_AOG_STATE_UNKNOWN;
    }
}

/* ---- Main collection function ---- */

void nav_health_collect(const nav_health_collector_t* coll,
                         nav_health_snapshot_t* snap,
                         uint64_t uptime_ms)
{
    if (snap == NULL) { return; }
    memset(snap, 0, sizeof(nav_health_snapshot_t));

    if (coll == NULL) {
        snap->uptime_ms = uptime_ms;
        return;
    }

    /* ---- UART ---- */
    extract_uart_fields(coll->uart_primary,
        &snap->uart_primary_rx_bytes, &snap->uart_primary_tx_bytes,
        &snap->uart_primary_rx_overflows, &snap->uart_primary_tx_errors);
    extract_uart_fields(coll->uart_secondary,
        &snap->uart_secondary_rx_bytes, &snap->uart_secondary_tx_bytes,
        &snap->uart_secondary_rx_overflows, &snap->uart_secondary_tx_errors);

    /* ---- GNSS Primary ---- */
    extract_gnss_fields(coll->gnss_primary,
        &snap->gnss_primary_valid, &snap->gnss_primary_fresh,
        &snap->gnss_primary_sentences, &snap->gnss_primary_checksum_err,
        &snap->gnss_primary_timeout_events, &snap->gnss_primary_bytes);

    /* ---- GNSS Secondary ---- */
    extract_gnss_fields(coll->gnss_secondary,
        &snap->gnss_secondary_valid, &snap->gnss_secondary_fresh,
        &snap->gnss_secondary_sentences, &snap->gnss_secondary_checksum_err,
        &snap->gnss_secondary_timeout_events, &snap->gnss_secondary_bytes);

    /* ---- Heading ---- */
    if (coll->heading != NULL) {
        snap->heading_valid    = coll->heading->result.valid;
        snap->heading_calc_count = coll->heading->calc_count;
    }

    /* ---- NTRIP ---- */
    if (coll->ntrip != NULL) {
        snap->ntrip_state          = map_ntrip_state(coll->ntrip->state);
        snap->ntrip_reconnect_count = coll->ntrip->reconnect_count;
        snap->ntrip_last_error     = (int)coll->ntrip->last_error;
        snap->ntrip_http_status    = coll->ntrip->http_status_code;
        /* RTCM bytes = bytes available in ntrip rtcm buffer (approximation) */
        snap->ntrip_bytes          = (uint32_t)byte_ring_buffer_available(
                                           &coll->ntrip->rtcm_buffer);
    }

    /* ---- RTCM Router ---- */
    if (coll->rtcm_router != NULL) {
        const rtcm_stats_t* rs = rtcm_router_get_stats(coll->rtcm_router);
        if (rs != NULL) {
            snap->rtcm_bytes_in     = rs->bytes_in;
            snap->rtcm_bytes_out    = rs->bytes_out;
            snap->rtcm_bytes_dropped = rs->bytes_dropped;
        }
        snap->rtcm_output_overflows = rtcm_router_get_output_overflow_count(
                                           coll->rtcm_router);
    }

    /* ---- AOG Navigation Output ---- */
    if (coll->aog_nav_app != NULL) {
        snap->aog_output_state = map_aog_state(coll->aog_nav_app->output_state);
        snap->aog_tx_frames    = coll->aog_nav_app->pgn214_send_count;
        snap->aog_suppressed   = coll->aog_nav_app->suppress_count;
        snap->aog_hello_count  = coll->aog_nav_app->hello_send_count;
        snap->aog_scan_count   = coll->aog_nav_app->scan_send_count;
    }

    /* ---- TCP ---- */
    if (coll->tcp != NULL) {
        snap->tcp_connected = coll->tcp->connected;
        snap->tcp_rx_bytes  = (uint32_t)byte_ring_buffer_available(
                                    &coll->tcp->rx_buffer);
        snap->tcp_tx_bytes  = (uint32_t)byte_ring_buffer_available(
                                    &coll->tcp->tx_buffer);
    }

    /* ---- Ethernet ---- */
    snap->eth_link_up = coll->eth_link_up;

    /* ---- System ---- */
    snap->uptime_ms          = uptime_ms;
    snap->last_error_module  = coll->last_error_module;
    snap->last_error_code    = coll->last_error_code;
    snap->total_errors       = coll->total_errors;
}

/* ---- State name helpers ---- */

const char* nav_ntrip_state_name(nav_ntrip_state_t state)
{
    switch (state) {
        case NAV_NTRIP_STATE_IDLE:           return "idle";
        case NAV_NTRIP_STATE_CONNECTING:     return "connecting";
        case NAV_NTRIP_STATE_BUILD_REQUEST:  return "build_request";
        case NAV_NTRIP_STATE_SEND_REQUEST:   return "send_request";
        case NAV_NTRIP_STATE_WAIT_RESPONSE:  return "wait_response";
        case NAV_NTRIP_STATE_CONNECTED:      return "connected";
        case NAV_NTRIP_STATE_ERROR:          return "error";
        case NAV_NTRIP_STATE_RETRY_WAIT:     return "retry_wait";
        default:                             return "unknown";
    }
}

const char* nav_aog_state_name(nav_aog_state_t state)
{
    switch (state) {
        case NAV_AOG_STATE_INIT:            return "init";
        case NAV_AOG_STATE_OK:              return "ok";
        case NAV_AOG_STATE_GNSS_INVALID:    return "gnss_invalid";
        case NAV_AOG_STATE_GNSS_STALE:      return "gnss_stale";
        case NAV_AOG_STATE_HEADING_INVALID: return "heading_invalid";
        case NAV_AOG_STATE_HEADING_STALE:   return "heading_stale";
        case NAV_AOG_STATE_HEADING_LOST:    return "heading_lost";
        case NAV_AOG_STATE_SUPPRESSED:      return "suppressed";
        default:                            return "unknown";
    }
}

const char* nav_error_module_name(nav_error_module_t module)
{
    switch (module) {
        case NAV_ERR_MODULE_NONE:           return "none";
        case NAV_ERR_MODULE_GNSS_PRIMARY:   return "gnss_primary";
        case NAV_ERR_MODULE_GNSS_SECONDARY: return "gnss_secondary";
        case NAV_ERR_MODULE_NTRIP:          return "ntrip";
        case NAV_ERR_MODULE_TCP:            return "tcp";
        case NAV_ERR_MODULE_ETH:            return "eth";
        case NAV_ERR_MODULE_UART_PRIMARY:   return "uart_primary";
        case NAV_ERR_MODULE_UART_SECONDARY: return "uart_secondary";
        case NAV_ERR_MODULE_RTCM:           return "rtcm";
        case NAV_ERR_MODULE_AOG:            return "aog";
        case NAV_ERR_MODULE_HEADING:        return "heading";
        default:                            return "unknown";
    }
}
