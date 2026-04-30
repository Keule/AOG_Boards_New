idf_component_register(
    SRCS "gnss_um980.c"
    INCLUDE_DIRS "."
    REQUIRES protocol_nmea gnss_snapshot runtime_buffers runtime_components
)
