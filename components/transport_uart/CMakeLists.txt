idf_component_register(
    SRCS "transport_uart.c"
    INCLUDE_DIRS "."
    REQUIRES hal_backend board_profiles hal_uart runtime_buffers runtime_components
)
