# hal_uart component — conditional platform split
#
# ESP-IDF builds:  hal_uart.c + hal_uart_esp32.c  (real UART driver)
# Native builds:   hal_uart.c + hal_uart_stub.c   (no-op stubs)
#
# ESP_PLATFORM is set by the ESP-IDF build system automatically.

set(HAL_UART_COMMON "hal_uart.c")

if(ESP_PLATFORM)
    set(HAL_UART_PLATFORM "hal_uart_esp32.c")
else()
    set(HAL_UART_PLATFORM "hal_uart_stub.c")
endif()

idf_component_register(
    SRCS ${HAL_UART_COMMON} ${HAL_UART_PLATFORM}
    INCLUDE_DIRS "."
    REQUIRES hal_backend board_profiles driver log
)
