idf_component_register(
    SRCS "nav_rtcm_wiring.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES rtcm_router board_profiles runtime_buffers
)
