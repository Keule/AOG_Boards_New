#pragma once

#include <stdint.h>
#include <stddef.h>
#include "hal_backend.h"
#include "board_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Use board_uart_port_t from board_profile.h */

/* ---- HAL UART Config ---- */

typedef struct {
    uint32_t baudrate;
    uint8_t  data_bits;
    uint8_t  stop_bits;
    uint8_t  parity;    /* 0=none, 1=even, 2=odd */
} hal_uart_config_t;

/* Default config macro */
#define HAL_UART_CONFIG_DEFAULT() { \
    .baudrate  = 115200,            \
    .data_bits = 8,                 \
    .stop_bits = 1,                 \
    .parity    = 0                  \
}

/* ---- HAL UART Ops ---- */

typedef struct {
    hal_err_t (*init)(board_uart_port_t port, const hal_uart_config_t* config);
    hal_err_t (*deinit)(board_uart_port_t port);
    /* Nonblocking read: returns number of bytes read (0 if none available) */
    int (*read)(board_uart_port_t port, uint8_t* buf, size_t max_len);
    /* Nonblocking write: returns number of bytes written */
    int (*write)(board_uart_port_t port, const uint8_t* buf, size_t len);
} hal_uart_ops_t;

/* ---- HAL UART API ---- */

hal_err_t hal_uart_init(const hal_uart_ops_t* ops);
hal_err_t hal_uart_deinit(void);
hal_err_t hal_uart_port_init(board_uart_port_t port, const hal_uart_config_t* config);
hal_err_t hal_uart_port_deinit(board_uart_port_t port);
int hal_uart_read(board_uart_port_t port, uint8_t* buf, size_t max_len);
int hal_uart_write(board_uart_port_t port, const uint8_t* buf, size_t len);
hal_err_t hal_uart_flush(board_uart_port_t port);
const hal_uart_ops_t* hal_uart_esp32_ops(void);

#ifdef __cplusplus
}
#endif
