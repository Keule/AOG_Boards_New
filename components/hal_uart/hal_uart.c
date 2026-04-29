#include "hal_uart.h"

#if defined(ESP_PLATFORM)
#include "driver/uart.h"
#include "esp_log.h"
#endif

static const hal_uart_ops_t* s_uart_ops = NULL;

#if defined(ESP_PLATFORM)
/* Track which UART ports have been initialized (by board_uart_port_t index) */
static bool s_port_initialized[BOARD_UART_COUNT] = { false };

static const char* TAG = "HAL_UART";
#define ESP32_UART_RX_BUF_SIZE  1024
#define ESP32_UART_TX_BUF_SIZE   512
#endif

/* ---- Abstract HAL API ---- */

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

hal_err_t hal_uart_flush(board_uart_port_t port)
{
    if (s_uart_ops == NULL || s_uart_ops->read == NULL) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    /* Drain: read and discard all pending RX data */
    uint8_t tmp[64];
    int total_drained = 0;
    while (total_drained < 8192) {  /* Safety limit to prevent infinite loop */
        int n = s_uart_ops->read(port, tmp, sizeof(tmp));
        if (n <= 0) break;
        total_drained += n;
    }
    return HAL_OK;
}

hal_err_t hal_uart_port_reset(board_uart_port_t port, const hal_uart_config_t* config)
{
    if (config == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }
    hal_err_t err = hal_uart_port_deinit(port);
    if (err != HAL_OK && err != HAL_ERR_NOT_INITIALIZED) {
        return err;
    }
    return hal_uart_port_init(port, config);
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

/* ==========================================================================
 * ESP32 Productive Implementations
 *
 * Full UART parametrisation via uart_config_t + uart_param_config().
 * No hardware flow control, deterministic configuration for UM980 GNSS.
 * ========================================================================== */

#if defined(ESP_PLATFORM)

/* Map hal_uart_config_t data_bits to ESP-IDF uart_word_length_t */
static int map_data_bits(uint8_t bits)
{
    switch (bits) {
        case 5: return UART_DATA_5_BITS;
        case 6: return UART_DATA_6_BITS;
        case 7: return UART_DATA_7_BITS;
        case 8:
        default: return UART_DATA_8_BITS;
    }
}

/* Map hal_uart_config_t stop_bits to ESP-IDF uart_stop_bits_t */
static int map_stop_bits(uint8_t bits)
{
    switch (bits) {
        case 2: return UART_STOP_BITS_2;
        case 1:
        default: return UART_STOP_BITS_1;
    }
}

/* Map hal_uart_config_t parity to ESP-IDF uart_parity_t */
static int map_parity(uint8_t parity)
{
    switch (parity) {
        case 1: return UART_PARITY_EVEN;
        case 2: return UART_PARITY_ODD;
        case 0:
        default: return UART_PARITY_DISABLE;
    }
}

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

    /* Step 1: Full UART parametrisation */
    uart_config_t uart_cfg = {
        .baud_rate  = (int)config->baudrate,
        .data_bits  = map_data_bits(config->data_bits),
        .parity     = map_parity(config->parity),
        .stop_bits  = map_stop_bits(config->stop_bits),
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(uart_num, &uart_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config(%d) failed: 0x%x", uart_num, (unsigned)err);
        return HAL_ERR_IO;
    }

    /* Step 2: Install UART driver */
    err = uart_driver_install(
        uart_num,
        ESP32_UART_RX_BUF_SIZE,
        ESP32_UART_TX_BUF_SIZE,
        0, NULL, 0
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install(%d) failed: 0x%x", uart_num, (unsigned)err);
        return HAL_ERR_IO;
    }

    /* Step 3: Configure GPIO pins */
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

    s_port_initialized[port] = true;

    ESP_LOGI(TAG, "UART port %d (num=%d) initialized: tx=%d rx=%d baud=%lu %d%c%s",
             (int)port, uart_num,
             pins->tx_pin, pins->rx_pin,
             (unsigned long)config->baudrate,
             config->data_bits,
             config->parity == 0 ? 'N' : (config->parity == 1 ? 'E' : 'O'),
             config->stop_bits == 2 ? "2" : "1");

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

    ESP_LOGI(TAG, "UART port %d (num=%d) deinitialized", (int)port, pins->uart_num);

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
    return written;  /* Return ACTUAL bytes written (may be < len) */
}

#else /* !ESP_PLATFORM — stub implementations for host/native tests */

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

#endif /* ESP_PLATFORM */

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
