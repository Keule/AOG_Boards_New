idf_component_register(
    SRCS "steering_diagnostics.c"
    INCLUDE_DIRS "."
    REQUIRES steering_control runtime_snapshot
)
