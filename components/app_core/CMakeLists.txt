idf_component_register(
    SRCS "app_core.c"
    INCLUDE_DIRS "."
    REQUIRES log feature_flags board_profiles runtime runtime_components
            transport_uart transport_udp transport_tcp
            gnss_um980 gnss_dual_heading
            ntrip_client rtcm_router
            aog_navigation_app
)
