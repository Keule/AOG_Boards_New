idf_component_register(
    SRCS "aog_navigation_app.c"
    INCLUDE_DIRS "."
    REQUIRES runtime_components gnss_um980 gnss_dual_heading protocol_aog transport_udp
)
