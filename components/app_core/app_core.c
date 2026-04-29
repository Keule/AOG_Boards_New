#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "app_core.h"
#include "feature_flags.h"
#include "board_profile.h"
#include "runtime.h"
#include "runtime_component.h"

/* ---- Navigation subsystem includes ----
 * Only compiled in when a navigation role is active. */
#if defined(DEVICE_ROLE_NAVIGATION) || defined(DEVICE_ROLE_FULL_TEST)
#include "hal_uart.h"
#include "transport_uart.h"
#include "transport_udp.h"
#include "transport_tcp.h"
#include "gnss_um980.h"
#include "gnss_dual_heading.h"
#include "ntrip_client.h"
#include "rtcm_router.h"
#include "aog_navigation_app.h"
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

#if defined(DEVICE_ROLE_NAVIGATION) || defined(DEVICE_ROLE_FULL_TEST)
    if (features & (FEATURE_ROLE_NAVIGATION | FEATURE_ROLE_FULL_TEST)) {
        ESP_LOGI(TAG, "Initializing navigation subsystem");

        /* Initialize HAL UART backend (ESP32) */
        hal_uart_init(hal_uart_esp32_ops());

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

        /* --- NTRIP client --- */
        ntrip_client_init(&s_ntrip);
        ntrip_client_set_tcp_source(&s_ntrip, &s_ntrip_tcp.rx_buffer);
        ntrip_client_start(&s_ntrip);

        /* --- RTCM router --- */
        rtcm_router_init(&s_rtcm_router);
        rtcm_router_set_source(&s_rtcm_router, &s_ntrip.rtcm_buffer);
        rtcm_router_add_output(&s_rtcm_router, &s_primary_uart.tx_buffer);
        rtcm_router_add_output(&s_rtcm_router, &s_secondary_uart.tx_buffer);

        /* --- AOG navigation app --- */
        aog_nav_app_init(&s_nav_app);
        aog_nav_app_set_position_source(&s_nav_app, &s_primary_gnss.position_snapshot);
        aog_nav_app_set_heading_source(&s_nav_app, gnss_dual_heading_get_snapshot(&s_heading));
        aog_nav_app_set_aog_rx_source(&s_nav_app, &s_aog_udp.rx_buffer);
        aog_nav_app_set_aog_tx_dest(&s_nav_app, &s_aog_udp.tx_buffer);

        /* --- Register navigation components --- */
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

        ESP_LOGI(TAG, "Navigation components registered: 10");

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

        /* --- Register steering components --- */
        runtime_component_register(&s_imu.component);
        runtime_component_register(&s_adc.component);
        runtime_component_register(&s_was.component);
        runtime_component_register(&s_steer_app.component);
        runtime_component_register(&s_steer_ctrl.component);
        runtime_component_register(&s_safety.component);
        runtime_component_register(&s_actuator.component);
        runtime_component_register(&s_steer_udp.component);

        ESP_LOGI(TAG, "Steering components registered: 8");

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
