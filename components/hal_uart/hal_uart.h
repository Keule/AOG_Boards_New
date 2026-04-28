#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_UART_PORT_CONSOLE = 0,
    HAL_UART_PORT_GNSS_PRIMARY,
    HAL_UART_PORT_GNSS_SECONDARY
} hal_uart_port_t;

typedef struct {
    hal_uart_port_t port;
    uint32_t baudrate;
} hal_uart_config_t;

typedef struct {
    hal_uart_port_t port;
    uint32_t baudrate;
    int initialized;
} hal_uart_t;

int hal_uart_init(hal_uart_t* uart, const hal_uart_config_t* config);
int hal_uart_deinit(hal_uart_t* uart);
int hal_uart_write_nonblocking(hal_uart_t* uart, const uint8_t* data, size_t length, size_t* written);
int hal_uart_read_nonblocking(hal_uart_t* uart, uint8_t* out_data, size_t max_length, size_t* read_count);

#ifdef __cplusplus
}
#endif
