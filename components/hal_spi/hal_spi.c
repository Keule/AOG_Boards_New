#include "hal_spi.h"

#include "hal_backend.h"
#include "hal_spi_backend.h"

int hal_spi_bus_init(hal_spi_bus_t* bus, const hal_spi_bus_config_t* config)
{
    if (hal_backend_get_kind() == HAL_BACKEND_SIM) {
        return hal_spi_backend_sim_bus_init(bus, config);
    }

    return hal_spi_backend_esp32_bus_init(bus, config);
}

int hal_spi_bus_deinit(hal_spi_bus_t* bus)
{
    if (hal_backend_get_kind() == HAL_BACKEND_SIM) {
        return hal_spi_backend_sim_bus_deinit(bus);
    }

    return hal_spi_backend_esp32_bus_deinit(bus);
}

int hal_spi_device_init(hal_spi_device_t* device, hal_spi_bus_t* bus, uint8_t chip_select)
{
    if (hal_backend_get_kind() == HAL_BACKEND_SIM) {
        return hal_spi_backend_sim_device_init(device, bus, chip_select);
    }

    return hal_spi_backend_esp32_device_init(device, bus, chip_select);
}

int hal_spi_transfer(hal_spi_device_t* device, const uint8_t* tx, uint8_t* rx, size_t length)
{
    if (hal_backend_get_kind() == HAL_BACKEND_SIM) {
        return hal_spi_backend_sim_transfer(device, tx, rx, length);
    }

    return hal_spi_backend_esp32_transfer(device, tx, rx, length);
}
