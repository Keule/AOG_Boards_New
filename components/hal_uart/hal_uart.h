#pragma once

/* Platform-neutral UART HAL.
 *
 * Only includes generic C headers and project headers that are themselves
 * platform-neutral (no ESP-IDF, no FreeRTOS).  ESP-IDF-specific code lives
 * exclusively in hal_uart_esp32.c; native builds use hal_uart_stub.c.
 *
 * This header MUST NOT include:
 *   driver/uart.h, esp_err.h, freertos/..., esp_log.h, etc.              */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "hal_backend.h"
#include "board_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- HAL UART Config ---- */

typedef struct {
    uint32_t baudrate;        /* Baud rate (e.g. 115200)                 */
    uint8_t  data_bits;       /* 5-9, typically 8                        */
    uint8_t  stop_bits;       /* 1 or 2                                  */
    uint8_t  parity;          /* 0=none, 1=even, 2=odd                  */
    int      tx_pin;          /* GPIO TX pin, -1 = use board default     */
    int      rx_pin;          /* GPIO RX pin, -1 = use board default     */
    size_t   rx_buffer_size;  /* 0 = use default (typically 256)         */
    size_t   tx_buffer_size;  /* 0 = use default (typically 256)         */
} hal_uart_config_t;

/* Default config macro */
#define HAL_UART_CONFIG_DEFAULT() {     \
    .baudrate       = 115200,           \
    .data_bits      = 8,                \
    .stop_bits      = 1,                \
    .parity         = 0,                \
    .tx_pin         = -1,               \
    .rx_pin         = -1,               \
    .rx_buffer_size = 0,                \
    .tx_buffer_size = 0                 \
}

/* ---- HAL UART Ops ---- */

typedef struct {
    hal_err_t (*init)(board_uart_port_t port, const hal_uart_config_t* config);
    hal_err_t (*deinit)(board_uart_port_t port);
    /* Nonblocking read: returns number of bytes read (0 if none available) */
    int (*read)(board_uart_port_t port, uint8_t* buf, size_t max_len);
    /* Nonblocking write: returns number of bytes written */
    int (*write)(board_uart_port_t port, const uint8_t* buf, size_t len);
    /* Flush TX/RX buffers */
    hal_err_t (*flush)(board_uart_port_t port);
    /* Reset UART peripheral (re-initialise) */
    hal_err_t (*reset)(board_uart_port_t port);
    /* Bytes available in RX buffer (0 if none) */
    int (*available)(board_uart_port_t port);
} hal_uart_ops_t;

/* ---- HAL UART API ---- */

/* Set the ops table.  Must be called before any port_* function.
 * Returns HAL_ERR_INVALID_PARAM if ops is NULL. */
hal_err_t hal_uart_init(const hal_uart_ops_t* ops);

/* Clear ops table (all subsequent calls return NOT_INITIALIZED). */
hal_err_t hal_uart_deinit(void);

/* Initialise a specific UART port with the given config.
 * Requires hal_uart_init() to have been called first.
 * Checks board_profile_has_uart() — returns HAL_ERR_NOT_SUPPORTED if
 * the port is not available on the current board. */
hal_err_t hal_uart_port_init(board_uart_port_t port, const hal_uart_config_t* config);

/* Deinitialise a specific UART port. */
hal_err_t hal_uart_port_deinit(board_uart_port_t port);

/* Nonblocking read.  Returns bytes read (0 if none). */
int hal_uart_read(board_uart_port_t port, uint8_t* buf, size_t max_len);

/* Nonblocking write.  Returns bytes written. */
int hal_uart_write(board_uart_port_t port, const uint8_t* buf, size_t len);

/* Flush UART TX/RX hardware buffers. */
hal_err_t hal_uart_flush(board_uart_port_t port);

/* Reset UART peripheral (re-initialise to defaults). */
hal_err_t hal_uart_reset(board_uart_port_t port);

/* Query number of bytes available in RX buffer. */
int hal_uart_available(board_uart_port_t port);

/* ---- Platform ops providers ---- */

/* ESP32 ops: real ESP-IDF UART driver (only callable on ESP32). */
const hal_uart_ops_t* hal_uart_esp32_ops(void);

/* Stub ops: no-op implementations for native / unsupported builds. */
const hal_uart_ops_t* hal_uart_stub_ops(void);

#ifdef __cplusplus
}
#endif
