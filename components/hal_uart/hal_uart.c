#include "hal_uart.h"

#if defined(ESP_PLATFORM) && !defined(UNIT_TEST)
#include "driver/uart.h"
#include "esp_log.h"
#endif

static const hal_uart_ops_t* s_uart_ops = NULL;

#if defined(ESP_PLATFORM) && !defined(UNIT_TEST)
/* Track which UART ports have been initialized (by board_uart_port_t index) */
static bool s_port_initialized[BOARD_UART_COUNT] = { false };

static const char* TAG = "HAL_UART";
#define ESP32_UART_RX_BUF_SIZE  1024
#define ESP32_UART_TX_BUF_SIZE   512
#endif

hal_err_t hal_uart_init(const hal_uart_ops_t* ops)
{
    if (ops == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }
    s_uart_ops = ops;
    return HAL_OK;
}

hal_err_t hal_uart_deinit(void)
{
    s_uart_ops = NULL;
    return HAL_OK;
}

hal_err_t hal_uart_port_init(board_uart_port_t port, const hal_uart_config_t* config)
{
    if (s_uart_ops == NULL || s_uart_ops->init == NULL) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    if (!board_profile_has_uart(port)) {
        return HAL_ERR_NOT_SUPPORTED;
    }
    return s_uart_ops->init(port, config);
}

hal_err_t hal_uart_port_deinit(board_uart_port_t port)
{
    if (s_uart_ops == NULL || s_uart_ops->deinit == NULL) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    return s_uart_ops->deinit(port);
}

int hal_uart_read(board_uart_port_t port, uint8_t* buf, size_t max_len)
{
    if (s_uart_ops == NULL || s_uart_ops->read == NULL) {
        return 0;
    }
    return s_uart_ops->read(port, buf, max_len);
}

int hal_uart_write(board_uart_port_t port, const uint8_t* buf, size_t len)
{
    if (s_uart_ops == NULL || s_uart_ops->write == NULL) {
        return 0;
    }
    return s_uart_ops->write(port, buf, len);
}

hal_err_t hal_uart_flush(board_uart_port_t port)
{
    /* Flush is optional — if ops has no flush, return not-supported */
    if (s_uart_ops == NULL) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    /* No flush op in current vtable — use read-drain on ESP32 side */
    (void)port;
    return HAL_ERR_NOT_SUPPORTED;
}

/* ---- ESP32 Implementations ---- */

#if defined(ESP_PLATFORM) && !defined(UNIT_TEST)

static hal_err_t esp32_uart_init(board_uart_port_t port, const hal_uart_config_t* config)
{
    if (port >= BOARD_UART_COUNT) {
        return HAL_ERR_INVALID_PARAM;
    }

    const board_uart_pin_config_t* pins = board_profile_get_uart_pins(port);
    if (pins == NULL) {
        ESP_LOGE(TAG, "No pin config for port %d", (int)port);
        return HAL_ERR_NOT_SUPPORTED;
    }

    int uart_num = pins->uart_num;

    /* Prevent double-init */
    if (s_port_initialized[port]) {
        ESP_LOGW(TAG, "UART port %d (num=%d) already initialized", (int)port, uart_num);
        return HAL_OK;
    }

    esp_err_t err = uart_driver_install(
        uart_num,
        ESP32_UART_RX_BUF_SIZE,
        ESP32_UART_TX_BUF_SIZE,
        0, NULL, 0
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install(%d) failed: 0x%x", uart_num, (unsigned)err);
        return HAL_ERR_IO;
    }

    err = uart_set_pin(
        uart_num,
        pins->tx_pin,
        pins->rx_pin,
        pins->rts_pin,
        pins->cts_pin
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin(%d) failed: 0x%x", uart_num, (unsigned)err);
        uart_driver_delete(uart_num);
        return HAL_ERR_IO;
    }

    err = uart_set_baudrate(uart_num, config->baudrate);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_baudrate(%d, %lu) failed: 0x%x",
                 uart_num, (unsigned long)config->baudrate, (unsigned)err);
        uart_driver_delete(uart_num);
        return HAL_ERR_IO;
    }

    s_port_initialized[port] = true;

    ESP_LOGI(TAG, "UART port %d (num=%d) initialized: tx=%d rx=%d baud=%lu",
             (int)port, uart_num, pins->tx_pin, pins->rx_pin,
             (unsigned long)config->baudrate);

    return HAL_OK;
}

static hal_err_t esp32_uart_deinit(board_uart_port_t port)
{
    if (port >= BOARD_UART_COUNT || !s_port_initialized[port]) {
        return HAL_ERR_NOT_INITIALIZED;
    }

    const board_uart_pin_config_t* pins = board_profile_get_uart_pins(port);
    if (pins == NULL) {
        return HAL_ERR_NOT_SUPPORTED;
    }

    uart_driver_delete(pins->uart_num);
    s_port_initialized[port] = false;

    return HAL_OK;
}

static int esp32_uart_read(board_uart_port_t port, uint8_t* buf, size_t max_len)
{
    if (port >= BOARD_UART_COUNT || !s_port_initialized[port]) {
        return 0;
    }

    const board_uart_pin_config_t* pins = board_profile_get_uart_pins(port);
    if (pins == NULL) {
        return 0;
    }

    /* timeout=0: non-blocking */
    int n = uart_read_bytes(pins->uart_num, buf, max_len, 0);
    return (n > 0) ? n : 0;
}

static int esp32_uart_write(board_uart_port_t port, const uint8_t* buf, size_t len)
{
    if (port >= BOARD_UART_COUNT || !s_port_initialized[port]) {
        return -1;
    }

    const board_uart_pin_config_t* pins = board_profile_get_uart_pins(port);
    if (pins == NULL) {
        return -1;
    }

    int written = uart_write_bytes(pins->uart_num, buf, len);
    if (written < 0) {
        return -1;
    }
    return (int)len;
}

#else /* !ESP_PLATFORM || UNIT_TEST — stub implementations for host tests */

static hal_err_t esp32_uart_init(board_uart_port_t port, const hal_uart_config_t* config)
{
    (void)port; (void)config;
    return HAL_OK;
}

static hal_err_t esp32_uart_deinit(board_uart_port_t port)
{
    (void)port;
    return HAL_OK;
}

static int esp32_uart_read(board_uart_port_t port, uint8_t* buf, size_t max_len)
{
    (void)port; (void)buf; (void)max_len;
    return 0;
}

static int esp32_uart_write(board_uart_port_t port, const uint8_t* buf, size_t len)
{
    (void)port; (void)buf; (void)len;
    return 0;
}

#endif /* ESP_PLATFORM && !UNIT_TEST */

static const hal_uart_ops_t s_esp32_uart_ops = {
    .init   = esp32_uart_init,
    .deinit = esp32_uart_deinit,
    .read   = esp32_uart_read,
    .write  = esp32_uart_write,
};

const hal_uart_ops_t* hal_uart_esp32_ops(void)
{
    return &s_esp32_uart_ops;
}
