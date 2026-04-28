#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_SPI_BUS_ETHERNET = 0,
    HAL_SPI_BUS_DEVICE,
    HAL_SPI_BUS_STORAGE
} hal_spi_bus_role_t;

typedef struct {
    hal_spi_bus_role_t role;
    uint32_t frequency_hz;
} hal_spi_bus_config_t;

typedef struct {
    hal_spi_bus_role_t role;
    uint32_t frequency_hz;
    int initialized;
} hal_spi_bus_t;

typedef struct {
    hal_spi_bus_t* bus;
    uint8_t chip_select;
    int initialized;
} hal_spi_device_t;

int hal_spi_bus_init(hal_spi_bus_t* bus, const hal_spi_bus_config_t* config);
int hal_spi_bus_deinit(hal_spi_bus_t* bus);
int hal_spi_device_init(hal_spi_device_t* device, hal_spi_bus_t* bus, uint8_t chip_select);
int hal_spi_transfer(hal_spi_device_t* device, const uint8_t* tx, uint8_t* rx, size_t length);

#ifdef __cplusplus
}
#endif
