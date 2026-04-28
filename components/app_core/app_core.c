#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "app_core.h"
#include "feature_flags.h"
#include "board_profile.h"
#include "runtime.h"
#include "runtime_component.h"

#if defined(DEVICE_ROLE_NAVIGATION) || defined(DEVICE_ROLE_FULL_TEST)
#include "transport_uart.h"
#include "transport_udp.h"
#include "transport_tcp.h"
#include "gnss_um980.h"
#include "gnss_dual_heading.h"
#include "ntrip_client.h"
#include "rtcm_router.h"
#include "aog_navigation_app.h"
#endif

#if defined(DEVICE_ROLE_STEERING) || defined(DEVICE_ROLE_FULL_TEST)
#include "transport_udp.h"
#include "imu_bno085.h"
#include "ads1118.h"
#include "was_sensor.h"
#include "steering_control.h"
#include "actuator_drv8263h.h"
#include "safety_failsafe.h"
#include "aog_steering_app.h"
#endif

static const char* TAG = "APP_CORE";

#if defined(DEVICE_ROLE_NAVIGATION) || defined(DEVICE_ROLE_FULL_TEST)
#define UM980_BAUDRATE          921600
#define AOG_UDP_LOCAL_PORT      9999
#define AOG_UDP_REMOTE_PORT     9999
#define NTRIP_DEFAULT_PORT      2101
#endif

#if defined(DEVICE_ROLE_STEERING) || defined(DEVICE_ROLE_FULL_TEST)
#define STEER_AOG_UDP_LOCAL_PORT   8888
#define STEER_AOG_UDP_REMOTE_PORT  8888
#define BNO085_CS_PIN              10U
#define ADS1118_CS_PIN             11U
#define DRV_PWM_PIN                20U
#define DRV_DIR_PIN                21U
#define FAILSAFE_PIN               22U
#define WAS_CALIB_MIN              (-16000)
#define WAS_CALIB_MAX              (16000)
#define SAFETY_TIMEOUT_MS          100U
#endif

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
static transport_udp_t      s_steer_udp;
static imu_bno085_t         s_imu_bno085;
static ads1118_t            s_ads1118;
static was_sensor_t         s_was_sensor;
static steering_control_t   s_steering_control;
static safety_failsafe_t    s_safety_failsafe;
static actuator_drv8263h_t  s_actuator_drv8263h;
static aog_steering_app_t   s_aog_steering_app;
#endif

void app_core_init(void)
{
    ESP_LOGI(TAG, "System init");

    runtime_init();

    unsigned int features = feature_flags_get();

#if defined(DEVICE_ROLE_NAVIGATION) || defined(DEVICE_ROLE_FULL_TEST)
    if (features & (FEATURE_ROLE_NAVIGATION | FEATURE_ROLE_FULL_TEST)) {
        ESP_LOGI(TAG, "Initializing navigation subsystem");

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

        gnss_um980_init(&s_primary_gnss, 0, "GNSS_PRIMARY");
        gnss_um980_init(&s_secondary_gnss, 1, "GNSS_SECONDARY");
        gnss_um980_set_rx_source(&s_primary_gnss, &s_primary_uart.rx_buffer);
        gnss_um980_set_rx_source(&s_secondary_gnss, &s_secondary_uart.rx_buffer);

        gnss_dual_heading_init(&s_heading);
        gnss_dual_heading_set_sources(&s_heading, &s_primary_gnss, &s_secondary_gnss);

        ntrip_client_init(&s_ntrip);
        ntrip_client_set_tcp_source(&s_ntrip, &s_ntrip_tcp.rx_buffer);

        rtcm_router_init(&s_rtcm_router);
        rtcm_router_set_source(&s_rtcm_router, &s_ntrip.rtcm_buffer);
        rtcm_router_add_output(&s_rtcm_router, &s_primary_uart.tx_buffer);
        rtcm_router_add_output(&s_rtcm_router, &s_secondary_uart.tx_buffer);

        aog_nav_app_init(&s_nav_app);
        aog_nav_app_set_position_source(&s_nav_app, &s_primary_gnss.position_snapshot);
        aog_nav_app_set_heading_source(&s_nav_app, gnss_dual_heading_get_snapshot(&s_heading));
        aog_nav_app_set_aog_rx_source(&s_nav_app, &s_aog_udp.rx_buffer);
        aog_nav_app_set_aog_tx_dest(&s_nav_app, &s_aog_udp.tx_buffer);

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
    }
#endif

#if defined(DEVICE_ROLE_STEERING) || defined(DEVICE_ROLE_FULL_TEST)
    if (features & (FEATURE_ROLE_STEERING | FEATURE_ROLE_FULL_TEST)) {
        ESP_LOGI(TAG, "Initializing steering subsystem");

        transport_udp_config_t steer_udp_cfg = {
            .local_port = STEER_AOG_UDP_LOCAL_PORT,
            .remote_ip = 0xFFFFFFFF,
            .remote_port = STEER_AOG_UDP_REMOTE_PORT
        };
        transport_udp_init(&s_steer_udp, &steer_udp_cfg);

        imu_bno085_init(&s_imu_bno085, BOARD_SPI_DEVICE, BNO085_CS_PIN);
        ads1118_init(&s_ads1118, BOARD_SPI_DEVICE, ADS1118_CS_PIN);
        was_sensor_init(&s_was_sensor, &s_ads1118, WAS_CALIB_MIN, WAS_CALIB_MAX);
        steering_control_init(&s_steering_control, &s_was_sensor, &s_imu_bno085);
        safety_failsafe_init(&s_safety_failsafe, &s_steering_control, SAFETY_TIMEOUT_MS);
        actuator_drv8263h_init(&s_actuator_drv8263h, &s_steering_control, &s_safety_failsafe, DRV_PWM_PIN, DRV_DIR_PIN, FAILSAFE_PIN, true);
        aog_steering_app_init(&s_aog_steering_app, &s_steer_udp, &s_steering_control, &s_safety_failsafe);

        runtime_component_register(&s_steer_udp.component);
        runtime_component_register(&s_imu_bno085.component);
        runtime_component_register(&s_ads1118.component);
        runtime_component_register(&s_was_sensor.component);
        runtime_component_register(&s_steering_control.component);
        runtime_component_register(&s_safety_failsafe.component);
        runtime_component_register(&s_actuator_drv8263h.component);
        runtime_component_register(&s_aog_steering_app.component);

        ESP_LOGI(TAG, "Steering components registered: 8");
    }
#endif

    if ((features & (FEATURE_ROLE_NAVIGATION | FEATURE_ROLE_STEERING | FEATURE_ROLE_FULL_TEST)) == 0U) {
        ESP_LOGI(TAG, "No role-specific components initialized");
    }
}

void app_core_start(void)
{
    runtime_start();
}
