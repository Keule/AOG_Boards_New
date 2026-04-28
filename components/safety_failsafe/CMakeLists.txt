idf_component_register(
    SRCS "safety_failsafe.c"
    INCLUDE_DIRS "."
    REQUIRES runtime_components steering_control
)
