#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "app_core.h"

#define TAG "app_core"
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
#include "nav_diagnostics.h"
#include "hal_eth.h"
#include "hw_runtime_diag.h"
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

/* TAG defined at top of file via #define TAG "app_core" */

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

/* ================================================================
 * WP-C: Pin Sanity Check
 *
 * Validates GPIO pin assignments at boot for the classic ESP32:
 *   - GPIO 0..33: input+output capable
 *   - GPIO 34..39: INPUT ONLY (cannot be used as TX)
 *
 * This check runs ONCE during app_core_init() and ABORTS further
 * init if a critical pin conflict is detected.
 * ================================================================ */

/* Classic ESP32 input-only GPIO range */
#define ESP32_INPUT_ONLY_GPIO_MIN  34
#define ESP32_INPUT_ONLY_GPIO_MAX  39

static bool is_esp32_input_only_gpio(int pin)
{
    return (pin >= ESP32_INPUT_ONLY_GPIO_MIN && pin <= ESP32_INPUT_ONLY_GPIO_MAX);
}

/**
 * Run sanity checks on board profile pin assignments.
 * Returns true if all checks pass, false if a critical error found.
 *
 * Checks:
 * 1. UART TX pins must NOT be input-only GPIOs (34..39 on classic ESP32)
 * 2. GPIO35 must NOT be used as TX (explicit hard rule from task spec)
 * 3. NAV role: Ethernet must be RMII, not W5500
 * 4. GNSS UART pins must be assigned and non-negative
 */
static bool board_pin_sanity_check(void)
{
    bool ok = true;

    ESP_LOGI(TAG, "=== BOARD PIN SANITY CHECK ===");

    /* ---- Check UART pins ---- */
    int uart_ports[] = {
        BOARD_UART_CONSOLE,
        BOARD_UART_GNSS_PRIMARY,
        BOARD_UART_GNSS_SECONDARY,
    };
    const char* uart_names[] = {
        "CONSOLE",
        "GNSS1",
        "GNSS2",
    };

    for (int i = 0; i < 3; i++) {
        board_uart_port_t port = (board_uart_port_t)uart_ports[i];
        if (!board_profile_has_uart(port)) {
            continue;  /* port not active on this build */
        }

        board_uart_pins_t pins;
        if (!board_profile_get_uart_pins(port, &pins)) {
            continue;
        }

        /* Check TX not input-only */
        if (is_esp32_input_only_gpio(pins.tx_pin)) {
            ESP_LOGE("BOARD_PIN",
                     "%s UART TX=GPIO%d is input-only (GPIO%d..%d)",
                     uart_names[i], pins.tx_pin,
                     ESP32_INPUT_ONLY_GPIO_MIN, ESP32_INPUT_ONLY_GPIO_MAX);
            ok = false;
        }

        /* Hard rule: GPIO35 must NOT be TX */
        if (pins.tx_pin == 35) {
            ESP_LOGE("BOARD_PIN",
                     "%s UART TX=GPIO35 violates hard rule (GPIO35 is input-only on classic ESP32)",
                     uart_names[i]);
            ok = false;
        }
    }

#if defined(DEVICE_ROLE_NAVIGATION) || defined(DEVICE_ROLE_FULL_TEST)
    /* ---- Check Ethernet type ---- */
    ethernet_kind_t eth = board_profile_get_eth();
    if (eth == ETH_W5500_SPI) {
        ESP_LOGE("BOARD_PIN",
                 "NAV role requires ETH_INTERNAL_MAC_RMII, but board reports ETH_W5500_SPI");
        ok = false;
    }
#endif

    if (ok) {
        ESP_LOGI(TAG, "=== BOARD PIN SANITY CHECK: PASS ===");
    } else {
        ESP_LOGE(TAG, "=== BOARD PIN SANITY CHECK: FAIL — init aborted ===");
    }

    return ok;
}

/* ================================================================
 * WP-B: Boot-Log — Pin Profile
 *
 * Prints the complete board pin profile at startup.
 * ================================================================ */

static void board_pin_boot_log(void)
{
    board_type_t board = board_profile_get_board();
    (void)board;  /* logged via BOARD_PROFILE_NAME macro above */

    /* ---- Line 1: Profile & Role ---- */
    const char* role = "UNKNOWN";
#if defined(DEVICE_ROLE_NAVIGATION)
    role = "NAV";
#elif defined(DEVICE_ROLE_STEERING)
    role = "STEER";
#elif defined(DEVICE_ROLE_FULL_TEST)
    role = "FULL_TEST";
#endif

    ESP_LOGI(TAG, "BOARD: profile=%s role=%s", BOARD_PROFILE_NAME, role);

    /* ---- Line 2: Ethernet ---- */
    board_eth_pins_t eth_pins;
    if (board_profile_get_eth_pins(&eth_pins)) {
        ESP_LOGI(TAG, "BOARD: eth=RTL8201/RMII clk=GPIO0_IN mdc=%d mdio=%d power=%d reset=%d",
                 eth_pins.mdc_pin, eth_pins.mdio_pin,
                 eth_pins.power_pin, eth_pins.reset_pin);
    } else {
        ethernet_kind_t eth = board_profile_get_eth();
        ESP_LOGI(TAG, "BOARD: eth=%d (no RMII pin details)", (int)eth);
    }

    /* ---- Lines 3-4: GNSS UARTs ---- */
    board_uart_pins_t uart_pins;
    if (board_profile_has_uart(BOARD_UART_GNSS_PRIMARY) &&
        board_profile_get_uart_pins(BOARD_UART_GNSS_PRIMARY, &uart_pins)) {
        ESP_LOGI(TAG, "BOARD: gnss1 uart=1 baud=%d tx=%d rx=%d",
                 BOARD_GNSS_UART_BAUDRATE, uart_pins.tx_pin, uart_pins.rx_pin);
    }
    if (board_profile_has_uart(BOARD_UART_GNSS_SECONDARY) &&
        board_profile_get_uart_pins(BOARD_UART_GNSS_SECONDARY, &uart_pins)) {
        ESP_LOGI(TAG, "BOARD: gnss2 uart=2 baud=%d tx=%d rx=%d",
                 BOARD_GNSS_UART_BAUDRATE, uart_pins.tx_pin, uart_pins.rx_pin);
    }

    /* ---- Line 5: SD Card ---- */
    board_sd_pins_t sd_pins;
    if (board_profile_get_sd_pins(&sd_pins)) {
        ESP_LOGI(TAG, "BOARD: sd miso=%d mosi=%d sclk=%d cs=%d",
                 sd_pins.miso_pin, sd_pins.mosi_pin,
                 sd_pins.sclk_pin, sd_pins.cs_pin);
    }

    /* ---- Line 6: Misc ---- */
    board_misc_pins_t misc_pins;
    if (board_profile_get_misc_pins(&misc_pins)) {
        ESP_LOGI(TAG, "BOARD: safety_in=%d log_switch=%d",
                 misc_pins.safety_in_pin, misc_pins.log_switch_pin);
    }
}

/* ---- Static instances (no heap allocation) ---- */
#if defined(DEVICE_ROLE_NAVIGATION) || defined(DEVICE_ROLE_FULL_TEST)

static transport_uart_t  s_primary_uart;
static transport_uart_t  s_secondary_uart;
static transport_udp_t   s_aog_udp;
static transport_tcp_t   s_ntrip_tcp;

static gnss_um980_t          s_primary_gnss;
static gnss_um980_t          s_secondary_gnss;
static gnss_dual_heading_calc_t s_heading;

static ntrip_client_t  s_ntrip;
static rtcm_router_t   s_rtcm_router;

static aog_nav_app_t   s_nav_app;

/* NAV-HW-RUNTIME-DIAG-001: Periodic hardware diagnostics */
static hw_runtime_diag_t s_hw_diag;

static nav_health_collector_t s_health_coll;
/* s_health_snap / s_log_recovery: reserved for future NAV-DIAG subsystem expansion */
static nav_health_snapshot_t  s_health_snap __attribute__((unused));
static nav_diag_log_entry_t   s_log_recovery __attribute__((unused));

/* ---- NAV-DIAG: ESP32 log bridge ---- */
static void app_core_diag_log_emit(nav_diag_level_t level,
                                     const char* module,
                                     const char* message)
{
    switch (level) {
        case NAV_DIAG_LEVEL_ERROR: ESP_LOGE("DIAG", "[%s] %s", module, message); break;
        case NAV_DIAG_LEVEL_WARN:  ESP_LOGW("DIAG", "[%s] %s", module, message); break;
        case NAV_DIAG_LEVEL_INFO:  ESP_LOGI("DIAG", "[%s] %s", module, message); break;
        case NAV_DIAG_LEVEL_DEBUG: ESP_LOGD("DIAG", "[%s] %s", module, message); break;
    }
}

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

    /* ---- WP-B: Print board pin profile ---- */
    board_pin_boot_log();

    /* ---- WP-C: Sanity-check pin assignments ---- */
    if (!board_pin_sanity_check()) {
        ESP_LOGE(TAG, "Pin sanity check failed — aborting init");
        return;
    }

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

        /* --- Dual heading --- */
        gnss_dual_heading_init(&s_heading);
        gnss_dual_heading_set_sources(&s_heading, &s_primary_gnss, &s_secondary_gnss);

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
        aog_nav_app_set_position_source(&s_nav_app, gnss_um980_get_position_snapshot(&s_primary_gnss));
        aog_nav_app_set_heading_source(&s_nav_app, gnss_dual_heading_get_snapshot(&s_heading));
        aog_nav_app_set_aog_rx_source(&s_nav_app, &s_aog_udp.rx_buffer);
        aog_nav_app_set_aog_tx_dest(&s_nav_app, &s_aog_udp.tx_buffer);

        /* --- Assign service groups and register components ---
         * Each component is assigned to its dedicated Core-0 service task group. */
        s_primary_uart.component.service_group   = SERVICE_GROUP_UART;
        s_secondary_uart.component.service_group = SERVICE_GROUP_UART;
        s_primary_gnss.component.service_group   = SERVICE_GROUP_UART;
        s_secondary_gnss.component.service_group = SERVICE_GROUP_UART;
        s_heading.component.service_group        = SERVICE_GROUP_UART;

        s_ntrip_tcp.component.service_group      = SERVICE_GROUP_TCP_NTRIP;
        s_ntrip.component.service_group          = SERVICE_GROUP_TCP_NTRIP;

        s_rtcm_router.component.service_group    = SERVICE_GROUP_UART;

        s_aog_udp.component.service_group        = SERVICE_GROUP_UDP;
        /* NAV-FIX-001-R2 AP-C: s_nav_app uses fast_path hooks (not service_step).
         * service_group is irrelevant since service_step = NULL, but we
         * assign a group for component counting / diagnostics consistency. */
        s_nav_app.component.service_group        = SERVICE_GROUP_UDP;

        runtime_component_register(&s_primary_uart.component);
        runtime_component_register(&s_secondary_uart.component);
        runtime_component_register(&s_primary_gnss.component);
        runtime_component_register(&s_secondary_gnss.component);
        runtime_component_register(&s_heading.component);
        runtime_component_register(&s_ntrip_tcp.component);
        runtime_component_register(&s_ntrip.component);
        runtime_component_register(&s_rtcm_router.component);
        runtime_component_register(&s_aog_udp.component);
        runtime_component_register(&s_nav_app.component);

        /* --- NAV-DIAG: Health collector wiring --- */
        nav_health_collector_init(&s_health_coll);
        nav_health_collector_set_uart_primary(&s_health_coll, &s_primary_uart);
        nav_health_collector_set_uart_secondary(&s_health_coll, &s_secondary_uart);
        nav_health_collector_set_gnss_primary(&s_health_coll, &s_primary_gnss);
        nav_health_collector_set_gnss_secondary(&s_health_coll, &s_secondary_gnss);
        nav_health_collector_set_heading(&s_health_coll, &s_heading);
        nav_health_collector_set_ntrip(&s_health_coll, &s_ntrip);
        nav_health_collector_set_rtcm_router(&s_health_coll, &s_rtcm_router);
        nav_health_collector_set_aog_nav_app(&s_health_coll, &s_nav_app);
        nav_health_collector_set_tcp(&s_health_coll, &s_ntrip_tcp);
        nav_health_collector_set_eth_link(&s_health_coll, hal_eth_is_connected());

        /* --- NAV-DIAG: ESP32 log callback --- */
        nav_diag_log_set_emit_callback(app_core_diag_log_emit);
        ESP_LOGI(TAG, "NAV-DIAG health collector wired (10 subsystems)");

        /* --- NAV-HW-RUNTIME-DIAG-001: Hardware runtime diagnostics --- */
        hw_runtime_diag_init(&s_hw_diag);
        hw_runtime_diag_set_sources(&s_hw_diag,
                                     &s_primary_uart,
                                     &s_secondary_uart,
                                     &s_primary_gnss,
                                     &s_secondary_gnss,
                                     &s_heading,
                                     &s_nav_app);
        s_hw_diag.component.service_group = SERVICE_GROUP_DIAGNOSTICS;
        runtime_component_register(&s_hw_diag.component);

        ESP_LOGI(TAG, "Navigation components registered: 11 "
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
