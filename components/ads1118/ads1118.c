#include "ads1118.h"

#include <string.h>

#include "hal_spi.h"

void ads1118_init(ads1118_t* dev, board_spi_bus_t spi_bus, uint8_t cs_pin)
{
    hal_spi_device_config_t cfg = HAL_SPI_DEVICE_CONFIG_DEFAULT();

    if (dev == NULL) {
        return;
    }

    memset(dev, 0, sizeof(*dev));
    dev->component.name = "ads1118";
    dev->component.user_data = dev;
    dev->component.service_step = ads1118_service_step;
    dev->spi_bus = spi_bus;
    dev->cs_pin = cs_pin;
    dev->sample_rate_sps = ADS1118_RATE_128SPS;

    cfg.clock_hz = 1000000;
    cfg.cs_pin = cs_pin;

    (void)hal_spi_bus_init(spi_bus);
    (void)hal_spi_device_add(spi_bus, cs_pin, &cfg);
}

const ads1118_sample_t* ads1118_get_sample(const ads1118_t* dev)
{
    return (dev != NULL) ? &dev->sample : NULL;
}

void ads1118_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    ads1118_t* dev;
    uint8_t tx[2] = {0};
    uint8_t rx[2] = {0};

    if (comp == NULL || comp->user_data == NULL) {
        return;
    }

    dev = (ads1118_t*)comp->user_data;
    (void)hal_spi_transfer(dev->spi_bus, dev->cs_pin, tx, rx, sizeof(tx));

    dev->sample.raw = (int16_t)((((uint16_t)rx[0]) << 8) | rx[1]);
    dev->sample.valid = true;
    dev->sample.timestamp_us = timestamp_us;
}
