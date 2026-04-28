#include "hal_spi.h"

static const hal_spi_ops_t* s_spi_ops = NULL;

hal_err_t hal_spi_init(const hal_spi_ops_t* ops)
{
    if (ops == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }
    s_spi_ops = ops;
    return HAL_OK;
}

hal_err_t hal_spi_deinit(void)
{
    s_spi_ops = NULL;
    return HAL_OK;
}

hal_err_t hal_spi_bus_init(board_spi_bus_t bus)
{
    if (s_spi_ops == NULL || s_spi_ops->bus_init == NULL) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    if (!board_profile_has_spi(bus)) {
        return HAL_ERR_NOT_SUPPORTED;
    }
    return s_spi_ops->bus_init(bus);
}

hal_err_t hal_spi_bus_deinit(board_spi_bus_t bus)
{
    if (s_spi_ops == NULL || s_spi_ops->bus_deinit == NULL) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    return s_spi_ops->bus_deinit(bus);
}

hal_err_t hal_spi_device_add(board_spi_bus_t bus, uint8_t cs_pin, const hal_spi_device_config_t* config)
{
    if (s_spi_ops == NULL || s_spi_ops->device_add == NULL) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    return s_spi_ops->device_add(bus, cs_pin, config);
}

hal_err_t hal_spi_device_select(board_spi_bus_t bus, uint8_t cs_pin)
{
    if (s_spi_ops == NULL || s_spi_ops->device_select == NULL) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    return s_spi_ops->device_select(bus, cs_pin);
}

hal_err_t hal_spi_device_deselect(board_spi_bus_t bus, uint8_t cs_pin)
{
    if (s_spi_ops == NULL || s_spi_ops->device_deselect == NULL) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    return s_spi_ops->device_deselect(bus, cs_pin);
}

int hal_spi_transfer(board_spi_bus_t bus, uint8_t cs_pin, const uint8_t* tx, uint8_t* rx, size_t len)
{
    if (s_spi_ops == NULL || s_spi_ops->transfer == NULL) {
        return 0;
    }
    return s_spi_ops->transfer(bus, cs_pin, tx, rx, len);
}

/* ---- ESP32 Stub Implementations ---- */

static hal_err_t esp32_spi_bus_init(board_spi_bus_t bus)     { (void)bus; return HAL_OK; }
static hal_err_t esp32_spi_bus_deinit(board_spi_bus_t bus)   { (void)bus; return HAL_OK; }
static hal_err_t esp32_spi_device_add(board_spi_bus_t bus, uint8_t cs, const hal_spi_device_config_t* cfg)
    { (void)bus; (void)cs; (void)cfg; return HAL_OK; }
static hal_err_t esp32_spi_device_select(board_spi_bus_t bus, uint8_t cs)
    { (void)bus; (void)cs; return HAL_OK; }
static hal_err_t esp32_spi_device_deselect(board_spi_bus_t bus, uint8_t cs)
    { (void)bus; (void)cs; return HAL_OK; }
static int esp32_spi_transfer(board_spi_bus_t bus, uint8_t cs, const uint8_t* tx, uint8_t* rx, size_t len)
    { (void)bus; (void)cs; (void)tx; (void)rx; (void)len; return 0; }

static const hal_spi_ops_t s_esp32_spi_ops = {
    .bus_init       = esp32_spi_bus_init,
    .bus_deinit     = esp32_spi_bus_deinit,
    .device_add     = esp32_spi_device_add,
    .device_select  = esp32_spi_device_select,
    .device_deselect = esp32_spi_device_deselect,
    .transfer       = esp32_spi_transfer,
};

const hal_spi_ops_t* hal_spi_esp32_ops(void)
{
    return &s_esp32_spi_ops;
}
