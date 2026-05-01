idf_component_register(
    SRCS "app_core.c"
    INCLUDE_DIRS "."
    REQUIRES log feature_flags board_profiles runtime runtime_components
            hal_uart hal_eth
            transport_uart transport_udp transport_tcp
            gnss_um980 gnss_dual_heading
            ntrip_client rtcm_router nav_rtcm_wiring
            aog_navigation_app
            nav_diagnostics
            imu_bno085 ads1118 was_sensor
            steering_control actuator_drv8263h
            safety_failsafe aog_steering_app
)
