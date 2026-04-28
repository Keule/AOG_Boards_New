idf_component_register(
    SRCS "imu_bno085.c"
    INCLUDE_DIRS "."
    REQUIRES runtime_components hal_spi board_profiles
)
