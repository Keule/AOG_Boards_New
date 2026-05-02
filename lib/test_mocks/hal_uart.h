#pragma once
/*
 * Stub hal_uart.h for native tests.
 * Provides type definitions and function stubs needed by transport_uart.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "hal_backend.h"
#include "board_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t baudrate;
    uint8_t  data_bits;
    uint8_t  stop_bits;
    uint8_t  parity;
    int      tx_pin;
    int      rx_pin;
    size_t   rx_buffer_size;
    size_t   tx_buffer_size;
} hal_uart_config_t;

#define HAL_UART_CONFIG_DEFAULT() { \
    .baudrate       = 115200,      \
    .data_bits      = 8,           \
    .stop_bits      = 1,           \
    .parity         = 0,           \
    .tx_pin         = -1,          \
    .rx_pin         = -1,          \
    .rx_buffer_size = 0,           \
    .tx_buffer_size = 0            \
}

typedef struct {
    hal_err_t (*init)(board_uart_port_t port, const hal_uart_config_t* config);
    hal_err_t (*deinit)(board_uart_port_t port);
    int (*read)(board_uart_port_t port, uint8_t* buf, size_t max_len);
    int (*write)(board_uart_port_t port, const uint8_t* buf, size_t len);
    hal_err_t (*flush)(board_uart_port_t port);
    hal_err_t (*reset)(board_uart_port_t port);
    int (*available)(board_uart_port_t port);
} hal_uart_ops_t;

hal_err_t hal_uart_init(const hal_uart_ops_t* ops);
hal_err_t hal_uart_deinit(void);
hal_err_t hal_uart_port_init(board_uart_port_t port, const hal_uart_config_t* config);
hal_err_t hal_uart_port_deinit(board_uart_port_t port);
int hal_uart_read(board_uart_port_t port, uint8_t* buf, size_t max_len);
int hal_uart_write(board_uart_port_t port, const uint8_t* buf, size_t len);
hal_err_t hal_uart_flush(board_uart_port_t port);
hal_err_t hal_uart_reset(board_uart_port_t port);
int hal_uart_available(board_uart_port_t port);

const hal_uart_ops_t* hal_uart_esp32_ops(void);
const hal_uart_ops_t* hal_uart_stub_ops(void);

#ifdef __cplusplus
}
#endif
