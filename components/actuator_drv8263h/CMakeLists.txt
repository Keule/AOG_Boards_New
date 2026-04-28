idf_component_register(
    SRCS "actuator_drv8263h.c"
    INCLUDE_DIRS "."
    REQUIRES runtime_components hal_gpio steering_control safety_failsafe
)
