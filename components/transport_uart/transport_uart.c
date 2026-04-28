#include "transport_uart.h"

static hal_uart_t s_uart_console;
static hal_uart_t s_uart_gnss_primary;
static hal_uart_t s_uart_gnss_secondary;

static hal_uart_t* get_uart(hal_uart_port_t port)
{
    switch (port) {
        case HAL_UART_PORT_CONSOLE:
            return &s_uart_console;
        case HAL_UART_PORT_GNSS_PRIMARY:
            return &s_uart_gnss_primary;
        case HAL_UART_PORT_GNSS_SECONDARY:
            return &s_uart_gnss_secondary;
        default:
            return 0;
    }
}

int transport_uart_init(void)
{
    hal_uart_config_t cfg_console = {HAL_UART_PORT_CONSOLE, 115200};
    hal_uart_config_t cfg_primary = {HAL_UART_PORT_GNSS_PRIMARY, 921600};
    hal_uart_config_t cfg_secondary = {HAL_UART_PORT_GNSS_SECONDARY, 921600};

    if (hal_uart_init(&s_uart_console, &cfg_console) != 0) return -1;
    if (hal_uart_init(&s_uart_gnss_primary, &cfg_primary) != 0) return -1;
    if (hal_uart_init(&s_uart_gnss_secondary, &cfg_secondary) != 0) return -1;

    return 0;
}

int transport_uart_read_nonblocking(hal_uart_port_t port, uint8_t* out_data, size_t max_length, size_t* read_count)
{
    hal_uart_t* uart = get_uart(port);
    if (uart == 0) {
        return -1;
    }
    return hal_uart_read_nonblocking(uart, out_data, max_length, read_count);
}

int transport_uart_write_nonblocking(hal_uart_port_t port, const uint8_t* data, size_t length, size_t* written)
{
    hal_uart_t* uart = get_uart(port);
    if (uart == 0) {
        return -1;
    }
    return hal_uart_write_nonblocking(uart, data, length, written);
}
