/* ========================================================================
 * app_core.c — Application Core Orchestrator (NAV-UART-STABILIZING-R3)
 *
 * This file is the common entry point for all build roles (NAV, STEER, FULL).
 * Role-specific initialization has been SPLIT into separate compilation
 * units to prevent ESP-IDF 6.0 component dependency auto-scanner from
 * detecting steering-only #include directives during NAV builds.
 *
 * ESP-IDF 6.0 scans ALL #include directives in registered SRCS regardless
 * of #ifdef guards. To avoid false dependency resolution failures:
 *   - app_core_nav.c    — compiled only for NAVIGATION / FULL_TEST
 *   - app_core_steer.c  — compiled only for STEERING / FULL_TEST
 *   - app_core.c        — compiled for ALL roles (role-agnostic)
 *
 * HARD RULES:
 *   - No role-specific #include directives in this file.
 *   - No steering or navigation headers.
 *   - No Arduino code.
 * ======================================================================== */

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "app_core.h"
#include "feature_flags.h"
#include "runtime.h"
#include "runtime_component.h"
#include "hal_uart.h"

static const char* TAG = "APP_CORE";

/* ---- Forward declarations for role-specific init functions ----
 * Defined in app_core_nav.c and app_core_steer.c respectively.
 * These are only linked when the corresponding source file is compiled. */

void app_core_init_navigation(void);
void app_core_init_steering(void);

/* =================================================================
 * app_core_init — System initialization entry point
 *
 * Called from app_main(). Performs:
 *   1. HAL UART init (ESP32 only, skipped for unit tests)
 *   2. Role-specific subsystem initialization
 *
 * Role dispatch is controlled by compile-time defines AND runtime
 * feature flags for defense-in-depth.
 * ================================================================= */
void app_core_init(void)
{
    ESP_LOGI(TAG, "System init");

    /* ---- Central ESP32 HAL UART init ----
     * Register ESP32 ops exactly ONCE before any transport_uart_init().
     * This covers all build profiles: NAV, STEER, and FULL_TEST.
     * Native/host tests use hal_uart_stub_ops() or custom test ops instead.
     * hal_uart_init() is idempotent (sets s_uart_ops pointer), but
     * calling it once centrally avoids ambiguity and double-init confusion. */
#if !defined(UNIT_TEST) && !defined(NATIVE_TEST)
    hal_uart_init(hal_uart_esp32_ops());
#endif

    /* ---- Role-specific subsystem initialization ----
     * For FULL_TEST builds, both navigation and steering are initialized.
     * For single-role builds, only the relevant subsystem runs.
     * Each init function checks feature_flags_get() internally. */
#if defined(DEVICE_ROLE_NAVIGATION) || defined(DEVICE_ROLE_FULL_TEST)
    app_core_init_navigation();
#endif

#if defined(DEVICE_ROLE_STEERING) || defined(DEVICE_ROLE_FULL_TEST)
    app_core_init_steering();
#endif
}

void app_core_start(void)
{
    runtime_init();
    runtime_start();
}
