#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "runtime_component.h"
#include "board_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool valid;
    float roll_deg;
    float yaw_deg;
    float yawrate_dps;
    uint64_t timestamp_us;
} imu_bno085_sample_t;

typedef struct {
    runtime_component_t component;
    board_spi_bus_t spi_bus;
    uint8_t cs_pin;
    imu_bno085_sample_t sample;
} imu_bno085_t;

void imu_bno085_init(imu_bno085_t* imu, board_spi_bus_t spi_bus, uint8_t cs_pin);
const imu_bno085_sample_t* imu_bno085_get_sample(const imu_bno085_t* imu);
void imu_bno085_service_step(runtime_component_t* comp, uint64_t timestamp_us);

#ifdef __cplusplus
}
#endif
