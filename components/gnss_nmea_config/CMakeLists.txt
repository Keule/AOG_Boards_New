idf_component_register(
    SRCS "gnss_nmea_config.c"
    INCLUDE_DIRS "."
    REQUIRES runtime_components transport_uart gnss_um980
)
