#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "app_core.h"
#include "feature_flags.h"
#include "board_profile.h"
#include "runtime.h"
#include "runtime_component.h"
#include "hal_uart.h"

/* ---- Navigation subsystem includes ----
 * Only compiled in when a navigation role is active. */
#if defined(DEVICE_ROLE_NAVIGATION) || defined(DEVICE_ROLE_FULL_TEST)
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
#endif

/* ---- Steering subsystem includes ----
 * Only compiled in when a steering role is active. */
#if defined(DEVICE_ROLE_STEERING) || defined(DEVICE_ROLE_FULL_TEST)
#include "imu_bno085.h"
#include "ads1118.h"
#include "was_sensor.h"
#include "steering_control.h"
#include "actuator_drv8263h.h"
#include "safety_failsafe.h"
#include "aog_steering_app.h"
#include "transport_udp.h"
#endif

static const char* TAG = "APP_CORE";

/* ---- Constants ---- */
#if defined(DEVICE_ROLE_NAVIGATION) || defined(DEVICE_ROLE_FULL_TEST)
#define UM980_BAUDRATE          921600
#define AOG_UDP_LOCAL_PORT      9999
#define AOG_UDP_REMOTE_PORT     9999
#define NTRIP_DEFAULT_PORT      2101
#endif

#if defined(DEVICE_ROLE_STEERING) || defined(DEVICE_ROLE_FULL_TEST)
#define STEER_UDP_LOCAL_PORT      9999
#define STEER_UDP_REMOTE_PORT     9999
#define IMU_BNO085_CS_PIN        5
#define ADS1118_CS_PIN           6
#define ADS1118_DEFAULT_RATE     ADS1118_RATE_128SPS
#define FAILSAFE_PIN             7
#define DRV8263H_PWM_PIN         8
#define DRV8263H_DIR_PIN         9
#define SAFETY_WATCHDOG_TIMEOUT_US  100000u  /* 100 ms */
#endif

/* ---- Static instances (no heap allocation) ---- */
#if defined(DEVICE_ROLE_NAVIGATION) || defined(DEVICE_ROLE_FULL_TEST)

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

#endif

#if defined(DEVICE_ROLE_STEERING) || defined(DEVICE_ROLE_FULL_TEST)

static imu_bno085_t     s_imu;
static ads1118_t        s_adc;
static was_sensor_t      s_was;

static steering_control_t   s_steer_ctrl;
static actuator_drv8263h_t  s_actuator;

static safety_failsafe_t  s_safety;

static aog_steering_app_t s_steer_app;

static transport_udp_t   s_steer_udp;

#endif

/* ---- Init ---- */

void app_core_init(void)
{
    ESP_LOGI(TAG, "System init");

    unsigned int features = feature_flags_get();

    /* ---- Central ESP32 HAL UART init ----
     * Register ESP32 ops exactly ONCE before any transport_uart_init().
     * This covers all build profiles: NAV, STEER, and FULL_TEST.
     * Native/host tests use hal_uart_stub_ops() or custom test ops instead.
     * hal_uart_init() is idempotent (sets s_uart_ops pointer), but
     * calling it once centrally avoids ambiguity and double-init confusion. */
#if !defined(UNIT_TEST) && !defined(NATIVE_TEST)
    hal_uart_init(hal_uart_esp32_ops());
#endif

    /* ---- Native test HAL override ----
     * Host tests call hal_uart_init() with custom stub ops in their
     * own setUp().  The central ESP32 init above is skipped for
     * UNIT_TEST / NATIVE_TEST builds via the guard above. */

#if defined(DEVICE_ROLE_NAVIGATION) || defined(DEVICE_ROLE_FULL_TEST)
    if (features & (FEATURE_ROLE_NAVIGATION | FEATURE_ROLE_FULL_TEST)) {
        ESP_LOGI(TAG, "Initializing navigation subsystem");

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
            .remote_ip = 0,
            .remote_port = NTRIP_DEFAULT_PORT
        };
        transport_tcp_init(&s_ntrip_tcp, &tcp_cfg);

        /* --- GNSS receivers --- */
        gnss_um980_init(&s_primary_gnss,   0, "GNSS_PRIMARY");
        gnss_um980_init(&s_secondary_gnss, 1, "GNSS_SECONDARY");
        gnss_um980_set_rx_source(&s_primary_gnss,   &s_primary_uart.rx_buffer);
        gnss_um980_set_rx_source(&s_secondary_gnss, &s_secondary_uart.rx_buffer);

        /* --- NMEA config managers (NAV-UM980-LOG-SYNTAX-RATE-FIX-001) --- */
        gnss_nmea_config_init(&s_primary_nmea_config,   0, "GNSS_CFG_PRIMARY");
        gnss_nmea_config_init(&s_secondary_nmea_config, 1, "GNSS_CFG_SECONDARY");
        gnss_nmea_config_set_sources(&s_primary_nmea_config,
                                      &s_primary_uart, &s_primary_gnss);
        gnss_nmea_config_set_sources(&s_secondary_nmea_config,
                                      &s_secondary_uart, &s_secondary_gnss);

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
            gnss_um980_get_snapshot(&s_primary_gnss),
            gnss_um980_get_snapshot(&s_secondary_gnss));

        /* --- NTRIP client ---
         * Init, register, and configure — but do NOT start if config is invalid.
         * Starting with empty host/mountpoint would silently fail.
         * The client stays in IDLE until a valid config is loaded (e.g. from NVS)
         * and ntrip_client_start() is called explicitly. */
        ntrip_client_init(&s_ntrip);
        ntrip_client_set_transport(&s_ntrip, &s_ntrip_tcp);
        ntrip_client_configure(&s_ntrip, &(ntrip_client_config_t)NTRIP_CLIENT_CONFIG_DEFAULT());

        if (s_ntrip.config_valid) {
            ntrip_client_start(&s_ntrip);
            ESP_LOGI(TAG, "NTRIP client started with valid config");
        } else {
            ESP_LOGW(TAG, "NTRIP client initialised but NOT started "
                     "(empty config — host/mountpoint required)");
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

        /* Map transport_uart instances to GNSS ports.
         * s_primary_uart  corresponds to BOARD_UART_GNSS_PRIMARY
         * s_secondary_uart corresponds to BOARD_UART_GNSS_SECONDARY
         * Future: add s_tertiary_uart for BOARD_UART_GNSS_TERTIARY
         *
         * We iterate the board profile port table and resolve the
         * matching transport_uart instance by port index. */
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
            /* NAV-RTCM-001: Abort navigation init — not productive.
             * An active GNSS receiver without RTCM output is unacceptable.
             * The caller (runtime) should detect missing component
             * registrations and handle accordingly. */
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

        ESP_LOGI(TAG, "Navigation components registered: 12 "
                 "(uart=%u, udp=%u, tcp_ntrip=%u, diag=%u)",
                 (unsigned)runtime_component_count_group(SERVICE_GROUP_UART),
                 (unsigned)runtime_component_count_group(SERVICE_GROUP_UDP),
                 (unsigned)runtime_component_count_group(SERVICE_GROUP_TCP_NTRIP),
                 (unsigned)runtime_component_count_group(SERVICE_GROUP_DIAGNOSTICS));

        return;
    }
#endif

#if defined(DEVICE_ROLE_STEERING) || defined(DEVICE_ROLE_FULL_TEST)
    if (features & (FEATURE_ROLE_STEERING | FEATURE_ROLE_FULL_TEST)) {
        ESP_LOGI(TAG, "Initializing steering subsystem");

        /* --- Sensors --- */
        imu_bno085_init(&s_imu, BOARD_SPI_DEVICE, IMU_BNO085_CS_PIN);

        ads1118_init(&s_adc, BOARD_SPI_DEVICE, ADS1118_CS_PIN);
        ads1118_set_sample_rate(&s_adc, ADS1118_DEFAULT_RATE);

        /* --- WAS sensor --- */
        was_sensor_init(&s_was);
        was_sensor_set_adc(&s_was, &s_adc);
        was_sensor_set_calibration(&s_was, 0, 65535, -40.0f, 40.0f);

        /* --- AOG steering transport --- */
        transport_udp_config_t steer_udp_cfg = {
            .local_port = STEER_UDP_LOCAL_PORT,
            .remote_ip = 0xFFFFFFFF,
            .remote_port = STEER_UDP_REMOTE_PORT
        };
        transport_udp_init(&s_steer_udp, &steer_udp_cfg);

        /* --- AOG steering app (parse AOG RX, produce SteeringInput snapshot) --- */
        aog_steering_app_init(&s_steer_app);
        aog_steering_app_set_aog_rx_source(&s_steer_app, &s_steer_udp.rx_buffer);
        aog_steering_app_set_aog_tx_dest(&s_steer_app, &s_steer_udp.tx_buffer);

        /* --- Safety/Failsafe (produce safety status snapshot) ---
         * MUST be initialised before steering_control so the safety
         * target reference can be wired. */
        safety_failsafe_init(&s_safety, FAILSAFE_PIN, HAL_GPIO_HIGH);
        safety_failsafe_set_timeout(&s_safety, SAFETY_WATCHDOG_TIMEOUT_US);

        /* --- Steering control (consume SteeringInput + WAS + IMU snapshots,
         *      feed safety watchdog on valid command with sensors) --- */
        steering_control_init(&s_steer_ctrl);
        steering_control_set_steer_input(&s_steer_ctrl,
            aog_steering_app_get_steer_input_snapshot(&s_steer_app));
        steering_control_set_was(&s_steer_ctrl,
            was_sensor_get_snapshot(&s_was));
        steering_control_set_imu(&s_steer_ctrl,
            imu_bno085_get_snapshot(&s_imu));
        steering_control_set_safety_target(&s_steer_ctrl, &s_safety);

        /* --- Actuator (consume SteeringCommand + Safety snapshots) --- */
        actuator_drv8263h_init(&s_actuator, DRV8263H_PWM_PIN, DRV8263H_DIR_PIN);
        actuator_drv8263h_set_command_source(&s_actuator,
            steering_control_get_command_snapshot(&s_steer_ctrl));
        actuator_drv8263h_set_safety_source(&s_actuator,
            safety_failsafe_get_snapshot(&s_safety));

        /* --- Assign service groups and register components --- */
        s_imu.component.service_group          = SERVICE_GROUP_DIAGNOSTICS;
        s_adc.component.service_group          = SERVICE_GROUP_DIAGNOSTICS;
        s_was.component.service_group          = SERVICE_GROUP_DIAGNOSTICS;
        s_steer_app.component.service_group    = SERVICE_GROUP_UDP;
        s_steer_ctrl.component.service_group   = SERVICE_GROUP_DIAGNOSTICS;
        s_safety.component.service_group       = SERVICE_GROUP_DIAGNOSTICS;
        s_actuator.component.service_group     = SERVICE_GROUP_DIAGNOSTICS;
        s_steer_udp.component.service_group    = SERVICE_GROUP_UDP;

        runtime_component_register(&s_imu.component);
        runtime_component_register(&s_adc.component);
        runtime_component_register(&s_was.component);
        runtime_component_register(&s_steer_app.component);
        runtime_component_register(&s_steer_ctrl.component);
        runtime_component_register(&s_safety.component);
        runtime_component_register(&s_actuator.component);
        runtime_component_register(&s_steer_udp.component);

        ESP_LOGI(TAG, "Steering components registered: 8 "
                 "(uart=%u, udp=%u, tcp_ntrip=%u, diag=%u)",
                 (unsigned)runtime_component_count_group(SERVICE_GROUP_UART),
                 (unsigned)runtime_component_count_group(SERVICE_GROUP_UDP),
                 (unsigned)runtime_component_count_group(SERVICE_GROUP_TCP_NTRIP),
                 (unsigned)runtime_component_count_group(SERVICE_GROUP_DIAGNOSTICS));

        return;
    }
#endif

    ESP_LOGI(TAG, "No role-specific components initialized");
}

void app_core_start(void)
{
    runtime_init();
    runtime_start();
}
