idf_component_register(
    SRCS "steering_control.c"
    INCLUDE_DIRS "."
    REQUIRES runtime_components was_sensor imu_bno085
)
