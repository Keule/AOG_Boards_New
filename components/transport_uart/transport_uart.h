#pragma once

#include <stddef.h>
#include <stdint.h>

#include "hal_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

int transport_uart_init(void);
int transport_uart_read_nonblocking(hal_uart_port_t port, uint8_t* out_data, size_t max_length, size_t* read_count);
int transport_uart_write_nonblocking(hal_uart_port_t port, const uint8_t* data, size_t length, size_t* written);

#ifdef __cplusplus
}
#endif
