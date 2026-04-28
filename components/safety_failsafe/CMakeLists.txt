idf_component_register(
    SRCS "safety_failsafe.c"
    INCLUDE_DIRS "."
    REQUIRES runtime_components runtime_watchdog runtime_snapshot hal_gpio
)
