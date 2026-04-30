idf_component_register(
    SRCS "aog_navigation_app.c"
    INCLUDE_DIRS "."
    REQUIRES runtime_components runtime_buffers runtime_snapshot
            gnss_dual_heading gnss_snapshot protocol_aog protocol_nmea
)
