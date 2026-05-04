idf_component_register(
    SRCS "gnss_um980_control.c"
    INCLUDE_DIRS "include"
    REQUIRES log freertos transport_uart gnss_um980
)
