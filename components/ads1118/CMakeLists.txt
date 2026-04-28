idf_component_register(
    SRCS "ads1118.c"
    INCLUDE_DIRS "."
    REQUIRES runtime_components hal_spi board_profiles
)
