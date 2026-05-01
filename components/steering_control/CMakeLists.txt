idf_component_register(
    SRCS "steering_control.c"
    INCLUDE_DIRS "."
    REQUIRES runtime_components runtime_types runtime_snapshot protocol_aog steering_safety steering_output
)
