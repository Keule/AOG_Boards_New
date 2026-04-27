idf_component_register(
    SRCS "app_core.c"
    INCLUDE_DIRS "."
    REQUIRES log feature_flags board_profiles runtime
)
