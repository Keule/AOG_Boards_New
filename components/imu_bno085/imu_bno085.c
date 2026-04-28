#include "imu_bno085.h"
#include <string.h>

void imu_bno085_init(imu_bno085_t* imu, board_spi_bus_t bus, uint8_t cs_pin)
{
    if (imu == NULL) {
        return;
    }

    memset(imu, 0, sizeof(imu_bno085_t));
    imu->spi_bus = bus;
    imu->cs_pin = cs_pin;

    snapshot_buffer_init(&imu->imu_snapshot,
                        &imu->imu_storage,
                        sizeof(imu_bno085_data_t));

    /* Register service step callback */
    imu->component.service_step = imu_bno085_service_step;
}

void imu_bno085_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    (void)timestamp_us;
    imu_bno085_t* imu = (imu_bno085_t*)comp;
    if (imu == NULL) {
        return;
    }

    /* SPI read: send command, receive sensor data.
     * TODO: implement actual BNO085 SPI protocol (register read sequence). */
    uint8_t tx_buf[8];
    uint8_t rx_buf[8];
    memset(tx_buf, 0, sizeof(tx_buf));
    memset(rx_buf, 0, sizeof(rx_buf));

    hal_spi_transfer(imu->spi_bus, imu->cs_pin, tx_buf, rx_buf, sizeof(tx_buf));
    imu->read_count++;

    /* Parse stub: real implementation would decode BNO085 sensor reports. */
    imu->heading_rad   = 0.0;
    imu->roll_rad      = 0.0;
    imu->yawrate_rad_s = 0.0;
    imu->data_valid    = true;

    /* Publish to snapshot buffer for downstream consumers */
    imu_bno085_data_t data;
    data.heading_rad   = imu->heading_rad;
    data.roll_rad      = imu->roll_rad;
    data.yawrate_rad_s = imu->yawrate_rad_s;
    data.valid         = imu->data_valid;
    snapshot_buffer_set(&imu->imu_snapshot, &data);
}

const snapshot_buffer_t* imu_bno085_get_snapshot(const imu_bno085_t* imu)
{
    if (imu == NULL) {
        return NULL;
    }
    return &imu->imu_snapshot;
}
