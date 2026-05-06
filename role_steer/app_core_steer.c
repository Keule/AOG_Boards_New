/* ========================================================================
 * app_core_steer.c — Steering subsystem initialization (NAV-UART-STABILIZING-R2)
 *
 * SPLIT from app_core.c to prevent ESP-IDF 6.0 component dependency
 * auto-scanner from detecting steering-specific #include directives.
 * This file is ONLY compiled for STEERING and FULL_TEST builds.
 * ======================================================================== */

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "feature_flags.h"
#include "board_profile.h"
#include "runtime.h"
#include "runtime_component.h"
#include "hal_uart.h"

/* ---- Steering subsystem includes ---- */
#include "imu_bno085.h"
#include "ads1118.h"
#include "was_sensor.h"
#include "steering_control.h"
#include "actuator_drv8263h.h"
#include "safety_failsafe.h"
#include "aog_steering_app.h"
#include "transport_udp.h"

static const char* TAG = "APP_CORE";

/* ---- Constants ---- */
#define STEER_UDP_LOCAL_PORT      9999
#define STEER_UDP_REMOTE_PORT     9999
#define IMU_BNO085_CS_PIN        5
#define ADS1118_CS_PIN           6
#define ADS1118_DEFAULT_RATE     ADS1118_RATE_128SPS
#define FAILSAFE_PIN             7
#define DRV8263H_PWM_PIN         8
#define DRV8263H_DIR_PIN         9
#define SAFETY_WATCHDOG_TIMEOUT_US  100000u  /* 100 ms */

/* ---- Static instances (no heap allocation) ---- */
static imu_bno085_t     s_imu;
static ads1118_t        s_adc;
static was_sensor_t      s_was;

static steering_control_t   s_steer_ctrl;
static actuator_drv8263h_t  s_actuator;

static safety_failsafe_t  s_safety;

static aog_steering_app_t s_steer_app;

static transport_udp_t   s_steer_udp;

/* =================================================================
 * Steering subsystem init — called from app_core_init()
 *
 * Initializes all STEER components: IMU, ADC, WAS, safety, control,
 * actuator, and AOG steering app.
 * ================================================================= */
void app_core_init_steering(void)
{
    unsigned int features = feature_flags_get();

    if (!(features & (FEATURE_ROLE_STEERING | FEATURE_ROLE_FULL_TEST))) {
        return;
    }

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
}
