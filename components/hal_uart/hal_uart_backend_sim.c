#include "hal_uart_backend.h"

int hal_uart_backend_sim_init(hal_uart_t* uart, const hal_uart_config_t* config)
{
    if (uart == 0 || config == 0) {
        return -1;
    }

    uart->port = config->port;
    uart->baudrate = config->baudrate;
    uart->initialized = 1;
    return 0;
}

int hal_uart_backend_sim_deinit(hal_uart_t* uart)
{
    if (uart == 0) {
        return -1;
    }

    uart->initialized = 0;
    return 0;
}

int hal_uart_backend_sim_write_nonblocking(hal_uart_t* uart, const uint8_t* data, size_t length, size_t* written)
{
    if (uart == 0 || data == 0 || written == 0 || uart->initialized == 0) {
        return -1;
    }

    *written = length;
    return 0;
}

int hal_uart_backend_sim_read_nonblocking(hal_uart_t* uart, uint8_t* out_data, size_t max_length, size_t* read_count)
{
    (void)out_data;
    (void)max_length;

    if (uart == 0 || read_count == 0 || uart->initialized == 0) {
        return -1;
    }

    *read_count = 0;
    return 0;
}
