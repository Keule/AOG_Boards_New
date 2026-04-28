#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "runtime_component.h"
#include "snapshot_buffer.h"
#include "hal_spi.h"
#include "board_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- IMU Data (snapshot payload) ---- */

typedef struct {
    double heading_rad;
    double roll_rad;
    double yawrate_rad_s;
    bool valid;
} imu_bno085_data_t;

/* ---- BNO085 IMU Instance ----
 *
 * SPI-based 9-DOF IMU. Reads heading, roll, yawrate via Device-SPI.
 * Publishes data via snapshot_buffer for downstream consumers.
 *
 * IMPORTANT: runtime_component_t MUST be the first field for safe casting. */
typedef struct {
    runtime_component_t component;    /* MUST be first */

    board_spi_bus_t spi_bus;
    uint8_t cs_pin;

    /* Latest data */
    double heading_rad;
    double roll_rad;
    double yawrate_rad_s;
    bool data_valid;
    uint32_t read_count;

    /* Snapshot for downstream consumers */
    snapshot_buffer_t imu_snapshot;
    imu_bno085_data_t imu_storage;
} imu_bno085_t;

/* ---- API ---- */

/* Initialize BNO085 instance on given SPI bus and CS pin. */
void imu_bno085_init(imu_bno085_t* imu, board_spi_bus_t bus, uint8_t cs_pin);

/* Service step: SPI read, parse stub, publish snapshot.
 * Called by the runtime service loop. */
void imu_bno085_service_step(runtime_component_t* comp, uint64_t timestamp_us);

/* Get pointer to the IMU snapshot buffer (for wiring to consumers). */
const snapshot_buffer_t* imu_bno085_get_snapshot(const imu_bno085_t* imu);

#ifdef __cplusplus
}
#endif
