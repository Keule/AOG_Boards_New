idf_component_register(
    SRCS "steering_control.c"
    INCLUDE_DIRS "."
    REQUIRES runtime_components runtime_snapshot protocol_aog safety_failsafe
)
