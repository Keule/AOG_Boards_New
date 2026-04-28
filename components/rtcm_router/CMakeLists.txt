idf_component_register(
    SRCS "rtcm_router.c"
    INCLUDE_DIRS "."
    REQUIRES runtime_components protocol_rtcm transport_uart hal_uart
)
