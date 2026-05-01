#pragma once
/* ========================================================================
 * nav_health.h — NAV Health/Status Snapshot (NAV-DIAG-001 Teil 1)
 *
 * Zentraler Diagnose-Snapshot für das NAV-Device. Sammelt Status- und
 * Fehlerzähler aller Subsysteme (UART, GNSS, NTRIP, RTCM, Heading, AOG,
 * Ethernet) in einer einzigen struct.
 *
 * Konzept:
 *   - nav_health_collector_t: Holder mit Pointern auf alle Subsysteme
 *   - nav_health_snapshot_t:   Read-only Snapshot mit allen Werten
 *   - nav_health_collect():    Pollt alle Subsysteme und befüllt Snapshot
 *
 * KEINE Fachlogik, KEINE Transport-Aufrufe, KEINE UART-I/O.
 * Nur Datenaggregation.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Forward declarations (avoid heavy header includes) ---- */
typedef struct transport_uart transport_uart_t;
typedef struct transport_tcp  transport_tcp_t;
typedef struct gnss_um980     gnss_um980_t;
typedef struct ntrip_client   ntrip_client_t;
typedef struct rtcm_router    rtcm_router_t;
typedef struct gnss_dual_heading_calc gnss_dual_heading_calc_t;
typedef struct aog_nav_app    aog_nav_app_t;

/* ---- NTRIP State mirror (avoids ntrip_client.h dependency) ---- */
typedef enum {
    NAV_NTRIP_STATE_UNKNOWN = 0,
    NAV_NTRIP_STATE_IDLE,
    NAV_NTRIP_STATE_CONNECTING,
    NAV_NTRIP_STATE_BUILD_REQUEST,
    NAV_NTRIP_STATE_SEND_REQUEST,
    NAV_NTRIP_STATE_WAIT_RESPONSE,
    NAV_NTRIP_STATE_CONNECTED,
    NAV_NTRIP_STATE_ERROR,
    NAV_NTRIP_STATE_RETRY_WAIT,
} nav_ntrip_state_t;

/* ---- AOG Output State mirror (avoids aog_navigation_app.h dependency) ---- */
typedef enum {
    NAV_AOG_STATE_UNKNOWN = 0,
    NAV_AOG_STATE_INIT,
    NAV_AOG_STATE_OK,
    NAV_AOG_STATE_GNSS_INVALID,
    NAV_AOG_STATE_GNSS_STALE,
    NAV_AOG_STATE_HEADING_INVALID,
    NAV_AOG_STATE_HEADING_STALE,
    NAV_AOG_STATE_HEADING_LOST,
    NAV_AOG_STATE_SUPPRESSED,
} nav_aog_state_t;

/* ---- Health Status Fields ---- */

typedef struct {
    /* ---- UART Primary (GNSS) ---- */
    uint32_t uart_primary_rx_bytes;      /* total bytes received on primary UART */
    uint32_t uart_primary_tx_bytes;      /* total bytes sent on primary UART */
    uint32_t uart_primary_rx_overflows;  /* RX buffer overflow count */
    uint32_t uart_primary_tx_errors;     /* HAL write errors */

    /* ---- UART Secondary (Heading GNSS) ---- */
    uint32_t uart_secondary_rx_bytes;
    uint32_t uart_secondary_tx_bytes;
    uint32_t uart_secondary_rx_overflows;
    uint32_t uart_secondary_tx_errors;

    /* ---- GNSS Primary ---- */
    bool     gnss_primary_valid;         /* position_valid && motion_valid */
    bool     gnss_primary_fresh;         /* within freshness timeout */
    uint32_t gnss_primary_sentences;     /* sentences parsed */
    uint32_t gnss_primary_checksum_err;  /* checksum error count */
    uint32_t gnss_primary_timeout_events;/* freshness timeout events */
    uint32_t gnss_primary_bytes;         /* total bytes received */

    /* ---- GNSS Secondary ---- */
    bool     gnss_secondary_valid;
    bool     gnss_secondary_fresh;
    uint32_t gnss_secondary_sentences;
    uint32_t gnss_secondary_checksum_err;
    uint32_t gnss_secondary_timeout_events;
    uint32_t gnss_secondary_bytes;

    /* ---- Heading ---- */
    bool     heading_valid;
    uint32_t heading_calc_count;

    /* ---- NTRIP ---- */
    nav_ntrip_state_t ntrip_state;
    uint32_t ntrip_reconnect_count;
    uint32_t ntrip_bytes;            /* RTCM bytes received from caster */
    int      ntrip_last_error;       /* ntrip_err_t code */
    int      ntrip_http_status;      /* last HTTP status (0 = none) */

    /* ---- RTCM Router ---- */
    uint32_t rtcm_bytes_in;          /* total RTCM bytes into router */
    uint32_t rtcm_bytes_out;         /* total RTCM bytes forwarded */
    uint32_t rtcm_bytes_dropped;     /* dropped (output buffer full) */
    uint32_t rtcm_output_overflows;  /* overflow event count */

    /* ---- AOG Navigation Output ---- */
    nav_aog_state_t aog_output_state;
    uint32_t aog_tx_frames;          /* PGN 214 frames sent */
    uint32_t aog_suppressed;         /* suppressed output cycles */
    uint32_t aog_hello_count;        /* Hello responses sent */
    uint32_t aog_scan_count;         /* Scan replies sent */

    /* ---- TCP Transport (NTRIP link) ---- */
    bool     tcp_connected;
    uint32_t tcp_rx_bytes;
    uint32_t tcp_tx_bytes;

    /* ---- Ethernet ---- */
    bool     eth_link_up;

    /* ---- System ---- */
    uint64_t uptime_ms;              /* system uptime in milliseconds */
    uint32_t last_error_module;      /* module ID of last error (0 = none) */
    uint32_t last_error_code;        /* error code from last error module */
    uint32_t total_errors;           /* cumulative error count */
} nav_health_snapshot_t;

/* ---- Module IDs for last_error ---- */
typedef enum {
    NAV_ERR_MODULE_NONE = 0,
    NAV_ERR_MODULE_GNSS_PRIMARY,
    NAV_ERR_MODULE_GNSS_SECONDARY,
    NAV_ERR_MODULE_NTRIP,
    NAV_ERR_MODULE_TCP,
    NAV_ERR_MODULE_ETH,
    NAV_ERR_MODULE_UART_PRIMARY,
    NAV_ERR_MODULE_UART_SECONDARY,
    NAV_ERR_MODULE_RTCM,
    NAV_ERR_MODULE_AOG,
    NAV_ERR_MODULE_HEADING,
} nav_error_module_t;

/* ---- Health Collector ----
 *
 * Holds weak references (pointers) to all subsystem instances.
 * The collector does NOT own any of these objects.
 *
 * Usage:
 *   nav_health_collector_t coll;
 *   nav_health_collector_init(&coll);
 *   nav_health_collector_set_uart_primary(&coll, &s_uart_primary);
 *   nav_health_collector_set_gnss_primary(&coll, &s_gnss_primary);
 *   ...
 *
 *   nav_health_snapshot_t snap;
 *   nav_health_collect(&coll, &snap, uptime_ms);
 *   // snap now contains all diagnostic values
 */
typedef struct {
    /* Subsystem references (set by caller, NOT owned) */
    transport_uart_t*         uart_primary;
    transport_uart_t*         uart_secondary;
    gnss_um980_t*             gnss_primary;
    gnss_um980_t*             gnss_secondary;
    gnss_dual_heading_calc_t* heading;
    ntrip_client_t*           ntrip;
    rtcm_router_t*            rtcm_router;
    aog_nav_app_t*            aog_nav_app;
    transport_tcp_t*          tcp;

    /* Ethernet link state (set externally by app_core or monitoring) */
    bool                      eth_link_up;

    /* Error tracking (owned by collector) */
    uint32_t total_errors;
    uint32_t last_error_module;
    uint32_t last_error_code;
} nav_health_collector_t;

/* ---- API ---- */

/* Initialize collector (zeros all fields, NULLs all references). */
void nav_health_collector_init(nav_health_collector_t* coll);

/* ---- Setter functions (wiring, called during init) ---- */

void nav_health_collector_set_uart_primary(nav_health_collector_t* coll,
                                            transport_uart_t* uart);
void nav_health_collector_set_uart_secondary(nav_health_collector_t* coll,
                                              transport_uart_t* uart);
void nav_health_collector_set_gnss_primary(nav_health_collector_t* coll,
                                            gnss_um980_t* gnss);
void nav_health_collector_set_gnss_secondary(nav_health_collector_t* coll,
                                              gnss_um980_t* gnss);
void nav_health_collector_set_heading(nav_health_collector_t* coll,
                                       gnss_dual_heading_calc_t* heading);
void nav_health_collector_set_ntrip(nav_health_collector_t* coll,
                                     ntrip_client_t* ntrip);
void nav_health_collector_set_rtcm_router(nav_health_collector_t* coll,
                                           rtcm_router_t* router);
void nav_health_collector_set_aog_nav_app(nav_health_collector_t* coll,
                                           aog_nav_app_t* app);
void nav_health_collector_set_tcp(nav_health_collector_t* coll,
                                   transport_tcp_t* tcp);
void nav_health_collector_set_eth_link(nav_health_collector_t* coll,
                                         bool link_up);

/* ---- Error recording ---- */

/* Record an error event (updates total_errors, last_error_module/code). */
void nav_health_record_error(nav_health_collector_t* coll,
                              nav_error_module_t module, uint32_t code);

/* ---- Snapshot collection ---- */

/* Poll all referenced subsystems and fill snapshot.
 * Any NULL reference is treated as "not present" (zeros/false).
 * uptime_ms: current system uptime in milliseconds. */
void nav_health_collect(const nav_health_collector_t* coll,
                         nav_health_snapshot_t* snap,
                         uint64_t uptime_ms);

/* ---- Utility: state name helpers (for logging) ---- */

const char* nav_ntrip_state_name(nav_ntrip_state_t state);
const char* nav_aog_state_name(nav_aog_state_t state);
const char* nav_error_module_name(nav_error_module_t module);

#ifdef __cplusplus
}
#endif
