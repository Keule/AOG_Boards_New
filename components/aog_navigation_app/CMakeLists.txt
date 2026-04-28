idf_component_register(
    SRCS "aog_navigation_app.c"
    INCLUDE_DIRS "."
    REQUIRES runtime_queue runtime_components gnss_dual_heading gnss_um980
)
