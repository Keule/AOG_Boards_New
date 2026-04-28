idf_component_register(
    SRCS "hal_spi.c"
    INCLUDE_DIRS "."
    REQUIRES hal_backend board_profiles
)
