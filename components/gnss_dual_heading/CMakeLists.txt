idf_component_register(
    SRCS "gnss_dual_heading.c"
    INCLUDE_DIRS "."
    REQUIRES gnss_um980 runtime_components
)
