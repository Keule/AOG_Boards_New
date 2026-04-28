idf_component_register(
    SRCS "rtcm_router.c"
    INCLUDE_DIRS "."
    REQUIRES runtime_components runtime_queue protocol_rtcm ntrip_client hal_uart
)
