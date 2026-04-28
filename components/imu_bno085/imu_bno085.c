#include "imu_bno085.h"

#include <string.h>

#include "hal_spi.h"

void imu_bno085_init(imu_bno085_t* imu, board_spi_bus_t spi_bus, uint8_t cs_pin)
{
    hal_spi_device_config_t cfg = HAL_SPI_DEVICE_CONFIG_DEFAULT();

    if (imu == NULL) {
        return;
    }

    memset(imu, 0, sizeof(*imu));
    imu->component.name = "imu_bno085";
    imu->component.user_data = imu;
    imu->component.service_step = imu_bno085_service_step;
    imu->spi_bus = spi_bus;
    imu->cs_pin = cs_pin;

    cfg.clock_hz = 1000000;
    cfg.cs_pin = cs_pin;

    (void)hal_spi_bus_init(spi_bus);
    (void)hal_spi_device_add(spi_bus, cs_pin, &cfg);
}

const imu_bno085_sample_t* imu_bno085_get_sample(const imu_bno085_t* imu)
{
    return (imu != NULL) ? &imu->sample : NULL;
}

void imu_bno085_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    imu_bno085_t* imu;
    uint8_t tx[4] = {0};
    uint8_t rx[4] = {0};

    if (comp == NULL || comp->user_data == NULL) {
        return;
    }

    imu = (imu_bno085_t*)comp->user_data;
    (void)hal_spi_transfer(imu->spi_bus, imu->cs_pin, tx, rx, sizeof(tx));

    imu->sample.valid = true;
    imu->sample.roll_deg = (float)((int8_t)rx[0]);
    imu->sample.yaw_deg = (float)((int8_t)rx[1]);
    imu->sample.yawrate_dps = (float)((int8_t)rx[2]);
    imu->sample.timestamp_us = timestamp_us;
}
