#pragma once

#include <stddef.h>
#include <stdint.h>

#include "hal_spi.h"

int hal_spi_backend_esp32_bus_init(hal_spi_bus_t* bus, const hal_spi_bus_config_t* config);
int hal_spi_backend_esp32_bus_deinit(hal_spi_bus_t* bus);
int hal_spi_backend_esp32_device_init(hal_spi_device_t* device, hal_spi_bus_t* bus, uint8_t chip_select);
int hal_spi_backend_esp32_transfer(hal_spi_device_t* device, const uint8_t* tx, uint8_t* rx, size_t length);
int hal_spi_backend_sim_bus_init(hal_spi_bus_t* bus, const hal_spi_bus_config_t* config);
int hal_spi_backend_sim_bus_deinit(hal_spi_bus_t* bus);
int hal_spi_backend_sim_device_init(hal_spi_device_t* device, hal_spi_bus_t* bus, uint8_t chip_select);
int hal_spi_backend_sim_transfer(hal_spi_device_t* device, const uint8_t* tx, uint8_t* rx, size_t length);
