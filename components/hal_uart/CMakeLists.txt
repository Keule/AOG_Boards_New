idf_component_register(
    SRCS "hal_uart.c"
    INCLUDE_DIRS "."
    REQUIRES hal_backend board_profiles
)
