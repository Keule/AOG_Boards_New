idf_component_register(
    SRCS "hal_uart.c" "hal_uart_backend_esp32.c" "hal_uart_backend_sim.c"
    INCLUDE_DIRS "."
    REQUIRES hal_backend
)
