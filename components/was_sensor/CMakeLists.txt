idf_component_register(
    SRCS "was_sensor.c"
    INCLUDE_DIRS "."
    REQUIRES runtime_components runtime_snapshot ads1118
)
