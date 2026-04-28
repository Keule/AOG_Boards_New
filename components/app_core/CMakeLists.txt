idf_component_register(
    SRCS "app_core.c"
    INCLUDE_DIRS "."
    REQUIRES log board_profiles feature_flags runtime runtime_buffers runtime_queue runtime_components gnss_um980 gnss_dual_heading ntrip_client rtcm_router aog_navigation_app transport_uart transport_udp transport_tcp
)
