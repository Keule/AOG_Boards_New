idf_component_register(
    SRCS "rtcm_router.c"
    INCLUDE_DIRS "."
    REQUIRES protocol_rtcm runtime_buffers runtime_components
)
