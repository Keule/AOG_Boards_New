idf_component_register(
    SRCS "runtime.c"
    INCLUDE_DIRS "."
    REQUIRES runtime_fast runtime_components gnss_um980 gnss_dual_heading rtcm_router aog_navigation_app
)
