idf_component_register(
    SRCS "gnss_dual_heading.c"
    INCLUDE_DIRS "."
    REQUIRES gnss_snapshot runtime_snapshot runtime_components
)
