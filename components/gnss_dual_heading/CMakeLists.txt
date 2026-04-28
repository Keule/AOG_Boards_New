idf_component_register(
    SRCS "gnss_dual_heading.c"
    INCLUDE_DIRS "."
    REQUIRES runtime_components gnss_um980
)
