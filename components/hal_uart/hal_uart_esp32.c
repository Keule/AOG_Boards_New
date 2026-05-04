/* ESP32 UART HAL implementation.
 *
 * This file is ONLY compiled when ESP_PLATFORM is defined
 * (i.e. ESP-IDF / PlatformIO ESP32 builds).  It includes
 * ESP-IDF headers and uses the real UART driver.
 *
 * DO NOT include this file in native / host builds.           */

#include "hal_uart.h"

#if defined(ESP_PLATFORM)

#include "driver/uart.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "HAL_UART";

/* ---- Port mapping ---- */

static int map_port(board_uart_port_t port)
{
    switch (port) {
    case BOARD_UART_CONSOLE:        return UART_NUM_0;
    case BOARD_UART_GNSS_PRIMARY:   return UART_NUM_1;
    case BOARD_UART_GNSS_SECONDARY: return UART_NUM_2;
    default:                        return -1;
    }
}

/* ---- Track which ports have been installed ---- */

static bool s_installed[3] = { false, false, false };

/* ---- ESP32 ops implementation ---- */

static hal_err_t esp32_uart_init(board_uart_port_t port, const hal_uart_config_t* config)
{
    if (config == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }

    int uart_num = map_port(port);
    if (uart_num < 0 || uart_num >= 3) {
        ESP_LOGE(TAG, "Invalid port %d", (int)port);
        return HAL_ERR_INVALID_PARAM;
    }

    /* If already installed, deinstall first */
    if (s_installed[uart_num]) {
        uart_driver_delete(uart_num);
        s_installed[uart_num] = false;
    }

    /* Configure UART parameters */
    uart_config_t uart_cfg = {
        .baud_rate  = config->baudrate,
        .data_bits  = (config->data_bits == 7) ? UART_DATA_7_BITS :
                      UART_DATA_8_BITS,
        .parity     = (config->parity == 1) ? UART_PARITY_EVEN :
                      (config->parity == 2) ? UART_PARITY_ODD :
                      UART_PARITY_DISABLE,
        .stop_bits  = (config->stop_bits >= 2) ? UART_STOP_BITS_2 :
                      UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(uart_num, &uart_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %d", err);
        return HAL_ERR_IO;
    }

    /* Set pins (-1 = use board default / don't change) */
    int tx_pin = (config->tx_pin >= 0) ? config->tx_pin : UART_PIN_NO_CHANGE;
    int rx_pin = (config->rx_pin >= 0) ? config->rx_pin : UART_PIN_NO_CHANGE;

    err = uart_set_pin(uart_num, tx_pin, rx_pin,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %d", err);
        return HAL_ERR_IO;
    }

    /* Buffer sizes: fall back to TRANSPORT_UART buffer sizes
     * (RX 4096, TX 1024) — matched to ring buffer sizes to avoid
     * hardware FIFO overflow at 921600 baud (~12KB/s GNSS). */
    int rx_buf = (config->rx_buffer_size > 0) ? (int)config->rx_buffer_size : 4096;
    int tx_buf = (config->tx_buffer_size > 0) ? (int)config->tx_buffer_size : 1024;

    /* Install UART driver */
    err = uart_driver_install(uart_num, rx_buf, tx_buf, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %d", err);
        return HAL_ERR_IO;
    }

    s_installed[uart_num] = true;
    ESP_LOGI(TAG, "UART port %d (UART_NUM_%d) initialised @ %lu baud",
             (int)port, uart_num, (unsigned long)config->baudrate);

    return HAL_OK;
}

static hal_err_t esp32_uart_deinit(board_uart_port_t port)
{
    int uart_num = map_port(port);
    if (uart_num < 0 || uart_num >= 3) {
        return HAL_ERR_INVALID_PARAM;
    }

    if (s_installed[uart_num]) {
        uart_driver_delete(uart_num);
        s_installed[uart_num] = false;
    }

    return HAL_OK;
}

static int esp32_uart_read(board_uart_port_t port, uint8_t* buf, size_t max_len)
{
    int uart_num = map_port(port);
    if (uart_num < 0 || uart_num >= 3 || !s_installed[uart_num]) {
        return 0;
    }
    return uart_read_bytes(uart_num, buf, max_len, 0); /* timeout=0 → nonblocking */
}

static int esp32_uart_write(board_uart_port_t port, const uint8_t* buf, size_t len)
{
    int uart_num = map_port(port);
    if (uart_num < 0 || uart_num >= 3 || !s_installed[uart_num]) {
        return 0;
    }
    return (int)uart_write_bytes(uart_num, buf, len);
}

static hal_err_t esp32_uart_flush(board_uart_port_t port)
{
    int uart_num = map_port(port);
    if (uart_num < 0 || uart_num >= 3 || !s_installed[uart_num]) {
        return HAL_ERR_INVALID_PARAM;
    }
    /* Flush both TX and RX buffers */
    uart_flush_input(uart_num);
    /* Wait for TX to complete, then flush */
    ESP_ERROR_CHECK(uart_wait_tx_done(uart_num, pdMS_TO_TICKS(100)));
    return HAL_OK;
}

static hal_err_t esp32_uart_reset(board_uart_port_t port)
{
    int uart_num = map_port(port);
    if (uart_num < 0 || uart_num >= 3 || !s_installed[uart_num]) {
        return HAL_ERR_INVALID_PARAM;
    }
    /* Deinstall and let the caller re-init */
    uart_driver_delete(uart_num);
    s_installed[uart_num] = false;
    return HAL_OK;
}

static int esp32_uart_available(board_uart_port_t port)
{
    int uart_num = map_port(port);
    if (uart_num < 0 || uart_num >= 3 || !s_installed[uart_num]) {
        return 0;
    }
    size_t buffered = 0;
    uart_get_buffered_data_len(uart_num, &buffered);
    return (int)buffered;
}

/* ---- Ops table ---- */

static const hal_uart_ops_t s_esp32_uart_ops = {
    .init      = esp32_uart_init,
    .deinit    = esp32_uart_deinit,
    .read      = esp32_uart_read,
    .write     = esp32_uart_write,
    .flush     = esp32_uart_flush,
    .reset     = esp32_uart_reset,
    .available = esp32_uart_available,
};

const hal_uart_ops_t* hal_uart_esp32_ops(void)
{
    return &s_esp32_uart_ops;
}

#endif /* ESP_PLATFORM */
