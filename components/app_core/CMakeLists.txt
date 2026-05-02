# ========================================================================
# app_core CMakeLists.txt
#
# NAV-TEST-SPLIT-001 WP-D: Conditional REQUIRES based on build role.
#
# Navigation components are always required.
# Steering components are only required for STEERING/FULL_TEST builds.
# ========================================================================

set(APP_CORE_NAV_REQUIRES
    log feature_flags board_profiles runtime runtime_components
    hal_uart
    transport_uart transport_udp transport_tcp
    gnss_um980 gnss_dual_heading
    ntrip_client rtcm_router nav_rtcm_wiring
    aog_navigation_app
    nav_diagnostics
    hal_eth
    hw_runtime_diag
)

set(APP_CORE_STEER_REQUIRES
    imu_bno085 ads1118 was_sensor
    steering_control actuator_drv8263h
    safety_failsafe aog_steering_app
)

set(APP_CORE_ALL_REQUIRES ${APP_CORE_NAV_REQUIRES})

# Only add steering dependencies when building for steering or full test
if(AOG_BUILD_ROLE STREQUAL "STEERING" OR AOG_BUILD_ROLE STREQUAL "FULL_TEST")
    list(APPEND APP_CORE_ALL_REQUIRES ${APP_CORE_STEER_REQUIRES})
endif()

idf_component_register(
    SRCS "app_core.c"
    INCLUDE_DIRS "."
    REQUIRES ${APP_CORE_ALL_REQUIRES}
)
