#include "hal_uart.h"

#include "hal_backend.h"
#include "hal_uart_backend.h"

int hal_uart_init(hal_uart_t* uart, const hal_uart_config_t* config)
{
    if (hal_backend_get_kind() == HAL_BACKEND_SIM) {
        return hal_uart_backend_sim_init(uart, config);
    }

    return hal_uart_backend_esp32_init(uart, config);
}

int hal_uart_deinit(hal_uart_t* uart)
{
    if (hal_backend_get_kind() == HAL_BACKEND_SIM) {
        return hal_uart_backend_sim_deinit(uart);
    }

    return hal_uart_backend_esp32_deinit(uart);
}

int hal_uart_write_nonblocking(hal_uart_t* uart, const uint8_t* data, size_t length, size_t* written)
{
    if (hal_backend_get_kind() == HAL_BACKEND_SIM) {
        return hal_uart_backend_sim_write_nonblocking(uart, data, length, written);
    }

    return hal_uart_backend_esp32_write_nonblocking(uart, data, length, written);
}

int hal_uart_read_nonblocking(hal_uart_t* uart, uint8_t* out_data, size_t max_length, size_t* read_count)
{
    if (hal_backend_get_kind() == HAL_BACKEND_SIM) {
        return hal_uart_backend_sim_read_nonblocking(uart, out_data, max_length, read_count);
    }

    return hal_uart_backend_esp32_read_nonblocking(uart, out_data, max_length, read_count);
}
