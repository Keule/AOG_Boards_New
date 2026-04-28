idf_component_register(
    SRCS "gnss_um980.c"
    INCLUDE_DIRS "."
    REQUIRES runtime_components protocol_nmea transport_uart hal_uart
)
