/* ========================================================================
 * app_core_nav.c — Navigation subsystem initialization (NAV-UART-STABILIZING-R5)
 *
 * SPLIT from app_core.c to prevent ESP-IDF 6.0 component dependency
 * auto-scanner from detecting steering-specific #include directives.
 * This file is ONLY compiled for NAVIGATION and FULL_TEST builds.
 *
 * R3 changes:
 *   - A1/D1: Added hal_eth_init() call — Ethernet was never initialized
 *   - A2/D2: Added remote_diag_init() + registration — WebUI was never started
 *   - Boot order: ETH → UART → UDP → GNSS → NMEA config → NTRIP → RTCM → Diag
 *
 * R4 changes:
 *   - Aufgabe 1: Added board_profile_log_all() + pin sanity check at boot
 *   - FIX: hal_eth_init() now calls ops->init() (was only storing pointer)
 *   - Aufgabe 2: RemoteDiag/WebUI already independent from GNSS-Restore
 *   - Aufgabe 3: Init order confirmed: Board→ETH→UART→UDP→GNSS→NMEA→NTRIP→RTCM→Diag
 *   - Aufgabe 7: RemoteDiag warns when HTTP not started (periodic)
 *
 * R5 changes:
 *   - OTA: hal_ota_init() called early, OTA handlers registered on HTTPD
 *   - OTA: /ota GET/POST + /reboot POST endpoints active after HTTP start
 *   - OTA: Independent of GNSS-Restore, NTRIP, RTCM, AOG_NAV
 * ======================================================================== */

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "feature_flags.h"
#include "board_profile.h"
#include "runtime.h"
#include "runtime_component.h"
#include "hal_uart.h"
#include "hal_eth.h"
#include "hal_ota.h"

/* ---- Navigation subsystem includes ---- */
#include "transport_uart.h"
#include "transport_udp.h"
#include "transport_tcp.h"
#include "gnss_um980.h"
#include "gnss_dual_heading.h"
#include "ntrip_client.h"
#include "rtcm_router.h"
#include "nav_rtcm_wiring.h"
#include "aog_navigation_app.h"
#include "gnss_nmea_config.h"
#include "remote_diag.h"
#include "hw_runtime_diag.h"
#include "nav_ntrip_config.h"

static const char* TAG = "APP_CORE";

/* ---- Constants ---- */
#define UM980_BAUDRATE          921600
#define AOG_UDP_LOCAL_PORT      9999
#define AOG_UDP_REMOTE_PORT     9999
#define NTRIP_DEFAULT_PORT      2101

/* ---- Static instances (no heap allocation) ---- */
static transport_uart_t  s_primary_uart;
static transport_uart_t  s_secondary_uart;
static transport_udp_t   s_aog_udp;
static transport_tcp_t   s_ntrip_tcp;

static gnss_um980_t          s_primary_gnss;
static gnss_um980_t          s_secondary_gnss;
static gnss_dual_heading_calc_t s_heading;

static gnss_nmea_config_t  s_primary_nmea_config;
static gnss_nmea_config_t  s_secondary_nmea_config;

static ntrip_client_t  s_ntrip;
static rtcm_router_t   s_rtcm_router;

static aog_nav_app_t   s_nav_app;
static remote_diag_t   s_diag;
static hw_runtime_diag_t s_hw_diag;

/* =================================================================
 * Navigation subsystem init — called from app_core_init()
 *
 * Initializes all NAV components: transport, GNSS, NMEA config,
 * NTRIP, RTCM routing, AOG navigation.
 * ================================================================= */
void app_core_init_navigation(void)
{
    unsigned int features = feature_flags_get();

    if (!(features & (FEATURE_ROLE_NAVIGATION | FEATURE_ROLE_FULL_TEST))) {
        return;
    }

    ESP_LOGI(TAG, "Initializing navigation subsystem");

    /* --- R4 FIX: Board profile log + pin sanity check (Aufgabe 1) ---
     * Must be FIRST in init order — logs board, role, ETH/GNSS pins,
     * and runs pin sanity check before any hardware init. */
    board_profile_log_all();

    /* --- R5: HAL OTA init (before ETH, independent of all runtime) ---
     * Initializes OTA ops table so hal_ota_begin/write/end are ready.
     * OTA handlers are registered later when HTTPD starts (in remote_diag_service_step).
     * OTA is fully independent of GNSS-Restore, NTRIP, RTCM, AOG_NAV. */
    {
        hal_err_t ota_ret = hal_ota_init(hal_ota_esp32_ops());
        if (ota_ret != HAL_OK) {
            ESP_LOGW(TAG, "HAL OTA init failed (0x%x) — OTA endpoints will not work",
                     (unsigned)ota_ret);
        } else {
            ESP_LOGI(TAG, "HAL OTA initialized (partition=%s, next=%s)",
                     hal_ota_get_active_partition_label(),
                     hal_ota_get_next_partition_label());
        }
    }

    /* --- Ethernet init (R3 A1/D1, R4 FIX) ---
     * MUST be before UDP/NTRIP/RemoteDiag.
     * Initializes ESP32 EMAC, RTL8201 PHY, netif, DHCP.
     * Without this, no IP is available and HTTP never starts. */
    {
        hal_err_t eth_ret = hal_eth_init(hal_eth_esp32_ops());
        if (eth_ret != HAL_OK) {
            ESP_LOGW(TAG, "Ethernet init skipped/failed (0x%x)", (unsigned)eth_ret);
        }
        /* Note: hal_eth_init triggers esp_netif_init() + esp_event_loop_create_default()
         * internally if not already done.  PHY power-on adds ~50ms one-time delay. */
    }

    /* --- Transport layer --- */
    if (board_profile_has_uart(BOARD_UART_GNSS_PRIMARY)) {
        transport_uart_config_t uart_cfg = {
            .port = BOARD_UART_GNSS_PRIMARY,
            .baudrate = UM980_BAUDRATE
        };
        transport_uart_init(&s_primary_uart, &uart_cfg);
    }

    if (board_profile_has_uart(BOARD_UART_GNSS_SECONDARY)) {
        transport_uart_config_t uart_cfg = {
            .port = BOARD_UART_GNSS_SECONDARY,
            .baudrate = UM980_BAUDRATE
        };
        transport_uart_init(&s_secondary_uart, &uart_cfg);
    }

    transport_udp_config_t udp_cfg = {
        .local_port = AOG_UDP_LOCAL_PORT,
        .remote_ip = 0xFFFFFFFF,
        .remote_port = AOG_UDP_REMOTE_PORT
    };
    transport_udp_init(&s_aog_udp, &udp_cfg);

    transport_tcp_config_t tcp_cfg = {
        .hostname = "",
        .remote_port = NTRIP_DEFAULT_PORT
    };
    transport_tcp_init(&s_ntrip_tcp, &tcp_cfg);

    /* --- GNSS receivers --- */
    gnss_um980_init(&s_primary_gnss,   0, "GNSS_PRIMARY");
    gnss_um980_init(&s_secondary_gnss, 1, "GNSS_SECONDARY");
    gnss_um980_set_rx_source(&s_primary_gnss,   &s_primary_uart.rx_buffer);
    gnss_um980_set_rx_source(&s_secondary_gnss, &s_secondary_uart.rx_buffer);

    /* --- NMEA config managers (NAV-UART-STABILIZING-R2) --- */
    gnss_nmea_config_init(&s_primary_nmea_config,   0, "GNSS_CFG_PRIMARY");
    gnss_nmea_config_init(&s_secondary_nmea_config, 1, "GNSS_CFG_SECONDARY");
    gnss_nmea_config_set_sources(&s_primary_nmea_config,
                                  &s_primary_uart, &s_primary_gnss);
    gnss_nmea_config_set_sources(&s_secondary_nmea_config,
                                  &s_secondary_uart, &s_secondary_gnss);

    /* ---- R6: UM980 Port Configuration (Aufgabe 3) ----
     * Both receivers default to COM2. If a receiver doesn't respond
     * to commands (ACK TIMEOUT), its port can be changed here:
     *
     *   gnss_nmea_config_set_um980_port(&s_primary_nmea_config, "COM1");
     *
     * Port matrix (to be verified on hardware):
     *   RX1 (UART1, GPIO TX=2 RX=4): COM2 (default, may need COM1)
     *   RX2 (UART2, GPIO TX=33 RX=34): COM2 (confirmed working)
     */

    /* Start NMEA restore on both receivers */
    if (board_profile_has_uart(BOARD_UART_GNSS_PRIMARY)) {
        gnss_nmea_config_start_restore(&s_primary_nmea_config);
    }
    if (board_profile_has_uart(BOARD_UART_GNSS_SECONDARY)) {
        gnss_nmea_config_start_restore(&s_secondary_nmea_config);
    }

    /* --- Dual heading --- */
    gnss_dual_heading_init(&s_heading);
    gnss_dual_heading_set_sources(&s_heading,
        &s_primary_gnss,
        &s_secondary_gnss);

    /* --- NTRIP client ---
     * Load config from compile-time build flags or local secrets file.
     * Priority: local_secrets > compile_time_flags > missing (disabled).
     * After loading config, TCP transport is re-initialized with the
     * real hostname so DNS resolution can work at connect time.
     * Without this, the TCP transport stays at hostname="" and
     * NTRIP can never establish a connection.
     * NTRIP_CFG_SRC_NVS_OVERRIDE is reserved for future NVS support. */
    ntrip_client_init(&s_ntrip);
    ntrip_client_set_transport(&s_ntrip, &s_ntrip_tcp);
    {
        ntrip_client_config_t ntrip_cfg;
        ntrip_config_source_t cfg_src = nav_ntrip_load_config(&ntrip_cfg);
        ntrip_client_configure(&s_ntrip, &ntrip_cfg);
        s_ntrip.config_source = (int)cfg_src;

        ESP_LOGI(TAG, "NTRIP config source: %s (%s)",
                 ntrip_config_source_name(cfg_src),
                 ntrip_config_is_ready(&ntrip_cfg) ? "valid" : "no valid config");

        if (ntrip_config_is_ready(&ntrip_cfg)) {
            /* Re-initialize TCP transport with the real hostname and port.
             * The initial TCP init above used empty hostname (placeholder).
             * Without this step, DNS resolution always fails → no NTRIP connection.
             * This matches the working reference implementation. */
            transport_tcp_disconnect(&s_ntrip_tcp);
            transport_tcp_config_t tcp_cfg_real = {
                .remote_port = ntrip_cfg.port
            };
            strncpy(tcp_cfg_real.hostname, ntrip_cfg.host,
                    TRANSPORT_TCP_HOSTNAME_MAX - 1);
            tcp_cfg_real.hostname[TRANSPORT_TCP_HOSTNAME_MAX - 1] = '\0';
            transport_tcp_init(&s_ntrip_tcp, &tcp_cfg_real);
            ntrip_client_set_transport(&s_ntrip, &s_ntrip_tcp);
        }
    }

    if (s_ntrip.config_valid) {
        /* Start NTRIP client — state machine is fully non-blocking.
         * If Ethernet/IP is not ready yet, the first connect attempt
         * will fail (DNS error), auto-retry with backoff, and succeed
         * once IP is available. Does NOT block boot, OTA, or RemoteDiag. */
        ntrip_client_start(&s_ntrip);
        ESP_LOGI(TAG, "NTRIP client started (config source: %s)",
                 ntrip_config_source_name((ntrip_config_source_t)s_ntrip.config_source));
    } else {
        ESP_LOGW(TAG, "NTRIP client initialised but NOT configured "
                 "(no build flags or secrets file — see platformio.ini or nav_ntrip_secrets.example.h)");
    }

    /* --- RTCM router ---
     * Source: NTRIP client RTCM buffer.
     * Outputs: all active GNSS receivers with valid TX pins.
     * Uses generic nav_rtcm_wire_outputs() which iterates over
     * the board profile GNSS port table (extensible to 3+ receivers).
     *
     * NAV-RTCM-001 rule: If ANY active GNSS receiver has no RTCM TX
     * path, the system is NOT productive.  The wiring function will
     * return productive=false with a clear error detail. */
    rtcm_router_init(&s_rtcm_router);
    rtcm_router_set_source(&s_rtcm_router, &s_ntrip.rtcm_buffer);

    /* Build target descriptor table (one entry per GNSS port candidate) */
    nav_rtcm_target_t gnss_targets[NAV_RTCM_MAX_TARGETS];
    int gnss_target_count = 0;

    /* Map transport_uart instances to GNSS ports. */
    transport_uart_t* uart_by_port[BOARD_UART_COUNT] = {0};
    uart_by_port[BOARD_UART_GNSS_PRIMARY]   = &s_primary_uart;
    uart_by_port[BOARD_UART_GNSS_SECONDARY] = &s_secondary_uart;

    for (int i = 0; i < board_profile_get_gnss_port_count() &&
                   gnss_target_count < NAV_RTCM_MAX_TARGETS; i++) {
        board_uart_port_t port = board_profile_get_gnss_port(i);
        gnss_targets[gnss_target_count].port = port;
        gnss_targets[gnss_target_count].tx_buffer =
            (uart_by_port[port] != NULL) ? &uart_by_port[port]->tx_buffer : NULL;
        gnss_target_count++;
    }

    /* Wire RTCM outputs generically */
    nav_rtcm_wiring_result_t wiring =
        nav_rtcm_wire_outputs(gnss_targets, gnss_target_count, &s_rtcm_router);

    if (!wiring.productive) {
        ESP_LOGE(TAG, "%s (active=%d, registered=%d)",
                 wiring.error_detail ? wiring.error_detail : "unknown error",
                 wiring.active_target_count,
                 wiring.registered_output_count);
        /* NAV-RTCM-001: Abort navigation init — not productive. */
        return;
    }

    ESP_LOGI(TAG, "RTCM router: %d output(s) wired for %d active GNSS receiver(s)",
             wiring.registered_output_count,
             wiring.active_target_count);

    /* --- AOG navigation app --- */
    aog_nav_app_init(&s_nav_app);
    aog_nav_app_set_position_source(&s_nav_app,
        gnss_um980_get_position_snapshot(&s_primary_gnss));
    aog_nav_app_set_heading_source(&s_nav_app,
        gnss_dual_heading_get_snapshot(&s_heading));
    aog_nav_app_set_aog_rx_source(&s_nav_app, &s_aog_udp.rx_buffer);
    aog_nav_app_set_aog_tx_dest(&s_nav_app, &s_aog_udp.tx_buffer);

    /* --- RemoteDiag init (R3 A2/D2) ---
     * Initialize HTTP server component, wire all observed references,
     * register in DIAGNOSTICS service group.
     * HTTP server starts automatically once IP is obtained (inside service_step). */
    remote_diag_init(&s_diag);
    remote_diag_set_sources(&s_diag,
        &s_primary_uart, &s_secondary_uart,
        &s_primary_gnss, &s_secondary_gnss,
        &s_heading, &s_nav_app, &s_aog_udp,
        &s_ntrip, &s_rtcm_router,
        &s_primary_nmea_config, &s_secondary_nmea_config);
    s_diag.component.service_group = SERVICE_GROUP_DIAGNOSTICS;

    /* --- R6: Hardware Runtime Diagnostics (Aufgabe 1) ---
     * Periodic health reporter for fast loop, GNSS UARTs, ETH/UDP,
     * NTRIP, PGN214 TX, RTCM pipeline, and GNSS_CFG final states.
     * Must be in SERVICE_GROUP_DIAGNOSTICS for 5s interval output. */
    hw_runtime_diag_init(&s_hw_diag);
    hw_runtime_diag_set_sources(&s_hw_diag,
        &s_primary_uart, &s_secondary_uart,
        &s_primary_gnss, &s_secondary_gnss,
        &s_heading, &s_nav_app, &s_aog_udp,
        &s_ntrip, &s_rtcm_router,
        &s_primary_nmea_config, &s_secondary_nmea_config);
    s_hw_diag.component.service_group = SERVICE_GROUP_DIAGNOSTICS;
    runtime_component_register(&s_hw_diag.component);

    /* --- Assign service groups and register components ---
     * Each component is assigned to its dedicated Core-0 service task group.
     * IMPORTANT: gnss_nmea_config MUST be registered AFTER UART but BEFORE
     * gnss_um980 so it intercepts the RX buffer during restore. */
    s_primary_uart.component.service_group   = SERVICE_GROUP_UART;
    s_secondary_uart.component.service_group = SERVICE_GROUP_UART;
    s_primary_nmea_config.component.service_group   = SERVICE_GROUP_UART;
    s_secondary_nmea_config.component.service_group = SERVICE_GROUP_UART;
    s_primary_gnss.component.service_group   = SERVICE_GROUP_UART;
    s_secondary_gnss.component.service_group = SERVICE_GROUP_UART;
    s_heading.component.service_group        = SERVICE_GROUP_UART;

    s_ntrip_tcp.component.service_group      = SERVICE_GROUP_TCP_NTRIP;
    s_ntrip.component.service_group          = SERVICE_GROUP_TCP_NTRIP;

    s_rtcm_router.component.service_group    = SERVICE_GROUP_UART;

    s_aog_udp.component.service_group        = SERVICE_GROUP_UDP;
    s_nav_app.component.service_group        = SERVICE_GROUP_DIAGNOSTICS;

    runtime_component_register(&s_primary_uart.component);
    runtime_component_register(&s_secondary_uart.component);
    runtime_component_register(&s_primary_nmea_config.component);
    runtime_component_register(&s_secondary_nmea_config.component);
    runtime_component_register(&s_primary_gnss.component);
    runtime_component_register(&s_secondary_gnss.component);
    runtime_component_register(&s_heading.component);
    runtime_component_register(&s_ntrip_tcp.component);
    runtime_component_register(&s_ntrip.component);
    runtime_component_register(&s_rtcm_router.component);
    runtime_component_register(&s_aog_udp.component);
    runtime_component_register(&s_nav_app.component);
    runtime_component_register(&s_diag.component);

    ESP_LOGI(TAG, "Navigation components registered: 14 "
             "(uart=%u, udp=%u, tcp_ntrip=%u, diag=%u)",
             (unsigned)runtime_component_count_group(SERVICE_GROUP_UART),
             (unsigned)runtime_component_count_group(SERVICE_GROUP_UDP),
             (unsigned)runtime_component_count_group(SERVICE_GROUP_TCP_NTRIP),
             (unsigned)runtime_component_count_group(SERVICE_GROUP_DIAGNOSTICS));
}
