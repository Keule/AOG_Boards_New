idf_component_register(
    SRCS "app_core.c"
    INCLUDE_DIRS "."
    REQUIRES log board_profiles feature_flags runtime
)
