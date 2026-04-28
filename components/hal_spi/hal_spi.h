#pragma once

#include <stdint.h>
#include <stddef.h>
#include "hal_backend.h"
#include "board_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Use board_spi_bus_t from board_profile.h */

/* ---- SPI Mode ---- */

typedef enum {
    HAL_SPI_MODE_0 = 0,  /* CPOL=0, CPHA=0 */
    HAL_SPI_MODE_1,      /* CPOL=0, CPHA=1 */
    HAL_SPI_MODE_2,      /* CPOL=1, CPHA=0 */
    HAL_SPI_MODE_3       /* CPOL=1, CPHA=1 */
} hal_spi_mode_t;

/* ---- SPI Config ---- */

typedef struct {
    uint32_t     clock_hz;
    hal_spi_mode_t mode;
    uint8_t      cs_pin;
} hal_spi_device_config_t;

#define HAL_SPI_DEVICE_CONFIG_DEFAULT() { \
    .clock_hz = 1000000,                  \
    .mode     = HAL_SPI_MODE_0,           \
    .cs_pin   = 0xFF                      \
}

/* ---- HAL SPI Ops ---- */

typedef struct {
    hal_err_t (*bus_init)(board_spi_bus_t bus);
    hal_err_t (*bus_deinit)(board_spi_bus_t bus);
    hal_err_t (*device_add)(board_spi_bus_t bus, uint8_t cs_pin, const hal_spi_device_config_t* config);
    hal_err_t (*device_select)(board_spi_bus_t bus, uint8_t cs_pin);
    hal_err_t (*device_deselect)(board_spi_bus_t bus, uint8_t cs_pin);
    int (*transfer)(board_spi_bus_t bus, uint8_t cs_pin, const uint8_t* tx, uint8_t* rx, size_t len);
} hal_spi_ops_t;

/* ---- HAL SPI API ---- */

hal_err_t hal_spi_init(const hal_spi_ops_t* ops);
hal_err_t hal_spi_deinit(void);
hal_err_t hal_spi_bus_init(board_spi_bus_t bus);
hal_err_t hal_spi_bus_deinit(board_spi_bus_t bus);
hal_err_t hal_spi_device_add(board_spi_bus_t bus, uint8_t cs_pin, const hal_spi_device_config_t* config);
hal_err_t hal_spi_device_select(board_spi_bus_t bus, uint8_t cs_pin);
hal_err_t hal_spi_device_deselect(board_spi_bus_t bus, uint8_t cs_pin);
int hal_spi_transfer(board_spi_bus_t bus, uint8_t cs_pin, const uint8_t* tx, uint8_t* rx, size_t len);
const hal_spi_ops_t* hal_spi_esp32_ops(void);

#ifdef __cplusplus
}
#endif
