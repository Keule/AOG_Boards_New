#pragma once

#include <stddef.h>
#include <stdint.h>

#include "hal_uart.h"

int hal_uart_backend_esp32_init(hal_uart_t* uart, const hal_uart_config_t* config);
int hal_uart_backend_esp32_deinit(hal_uart_t* uart);
int hal_uart_backend_esp32_write_nonblocking(hal_uart_t* uart, const uint8_t* data, size_t length, size_t* written);
int hal_uart_backend_esp32_read_nonblocking(hal_uart_t* uart, uint8_t* out_data, size_t max_length, size_t* read_count);
int hal_uart_backend_sim_init(hal_uart_t* uart, const hal_uart_config_t* config);
int hal_uart_backend_sim_deinit(hal_uart_t* uart);
int hal_uart_backend_sim_write_nonblocking(hal_uart_t* uart, const uint8_t* data, size_t length, size_t* written);
int hal_uart_backend_sim_read_nonblocking(hal_uart_t* uart, uint8_t* out_data, size_t max_length, size_t* read_count);
