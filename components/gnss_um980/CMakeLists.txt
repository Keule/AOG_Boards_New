idf_component_register(
    SRCS "gnss_um980.c"
    INCLUDE_DIRS "."
    REQUIRES runtime_components runtime_buffers protocol_nmea
)
