#include "hal_spi.h"

int hal_spi_bus_init(hal_spi_bus_t* bus, const hal_spi_bus_config_t* config)
{
    if (bus == 0 || config == 0) {
        return -1;
    }

    bus->role = config->role;
    bus->frequency_hz = config->frequency_hz;
    bus->initialized = 1;

    return 0;
}

int hal_spi_bus_deinit(hal_spi_bus_t* bus)
{
    if (bus == 0) {
        return -1;
    }

    bus->initialized = 0;
    return 0;
}

int hal_spi_device_init(hal_spi_device_t* device, hal_spi_bus_t* bus, uint8_t chip_select)
{
    if (device == 0 || bus == 0 || bus->initialized == 0) {
        return -1;
    }

    device->bus = bus;
    device->chip_select = chip_select;
    device->initialized = 1;

    return 0;
}

int hal_spi_transfer(hal_spi_device_t* device, const uint8_t* tx, uint8_t* rx, size_t length)
{
    size_t i = 0;

    if (device == 0 || device->initialized == 0) {
        return -1;
    }

    for (i = 0; i < length; i++) {
        uint8_t value = 0;

        if (tx != 0) {
            value = tx[i];
        }

        if (rx != 0) {
            rx[i] = value;
        }
    }

    return 0;
}
