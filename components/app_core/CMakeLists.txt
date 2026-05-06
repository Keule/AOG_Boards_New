# ========================================================================
# app_core CMakeLists.txt (NAV-UART-STABILIZING-R3)
#
# SPLIT STRATEGY:
#   app_core.c is the role-agnostic orchestrator (always compiled).
#   Role-specific source files live OUTSIDE components/ to prevent
#   ESP-IDF 6.0 from auto-scanning them for #include directives.
#
# BUILD ROLE LOGIC:
#   NAVIGATION: app_core.c + role_nav/app_core_nav.c
#   STEERING:   app_core.c + role_steer/app_core_steer.c
#   FULL_TEST:  app_core.c + role_nav/app_core_nav.c + role_steer/app_core_steer.c
#
# R3 CHANGE: Added hal_eth + remote_diag to NAV REQUIRES so that
#   app_core_nav.c can include hal_eth.h and remote_diag.h.
#
# NOTE: External source files need explicit include directories because
# ESP-IDF's REQUIRES mechanism only applies include paths to files
# within the component directory.
# ========================================================================

set(_aog_role "FULL_TEST")
set(_role_file "${CMAKE_SOURCE_DIR}/.aog_build_role")
if(EXISTS "${_role_file}")
    file(STRINGS "${_role_file}" _aog_role)
endif()

# Base sources — always compiled (role-agnostic, no steering includes)
set(_core_SRCS "app_core.c")

# Base REQUIRES — needed by app_core.c (role-agnostic)
set(_core_REQUIRES log feature_flags board_profiles runtime runtime_components hal_uart)

# Collect extra include directories for role-specific files
set(_extra_includes "")

# NAVIGATION role: add nav sources and dependencies
if(NOT _aog_role STREQUAL "STEERING")
    list(APPEND _core_SRCS "${CMAKE_SOURCE_DIR}/role_nav/app_core_nav.c")
    list(APPEND _core_REQUIRES
        hal_eth remote_diag hal_ota
        transport_uart transport_udp transport_tcp
        gnss_um980 gnss_dual_heading gnss_nmea_config
        ntrip_client rtcm_router nav_rtcm_wiring
        aog_navigation_app
        hw_runtime_diag remote_log nav_config)
    set(_nav_includes
        hal_eth remote_diag hal_ota
        transport_uart transport_udp transport_tcp
        gnss_um980 gnss_dual_heading gnss_nmea_config
        ntrip_client rtcm_router nav_rtcm_wiring
        aog_navigation_app
        hw_runtime_diag remote_log nav_config
        protocol_nmea gnss_snapshot runtime_buffers runtime_snapshot
        transport_eth)
    foreach(_comp ${_nav_includes})
        list(APPEND _extra_includes "${CMAKE_SOURCE_DIR}/components/${_comp}")
    endforeach()
endif()

# STEERING role: add steer sources and dependencies
if(NOT _aog_role STREQUAL "NAVIGATION")
    list(APPEND _core_SRCS "${CMAKE_SOURCE_DIR}/role_steer/app_core_steer.c")
    list(APPEND _core_REQUIRES
        transport_uart transport_udp
        imu_bno085 ads1118 was_sensor
        steering_control actuator_drv8263h
        safety_failsafe aog_steering_app)
    set(_steer_includes
        transport_uart transport_udp
        imu_bno085 ads1118 was_sensor
        steering_control actuator_drv8263h
        safety_failsafe aog_steering_app
        steering_safety steering_output)
    foreach(_comp ${_steer_includes})
        list(APPEND _extra_includes "${CMAKE_SOURCE_DIR}/components/${_comp}")
    endforeach()
endif()

idf_component_register(
    SRCS ${_core_SRCS}
    INCLUDE_DIRS "." ${_extra_includes}
    REQUIRES ${_core_REQUIRES}
)
