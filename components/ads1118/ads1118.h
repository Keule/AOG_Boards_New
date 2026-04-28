#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "runtime_component.h"
#include "board_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ADS1118_RATE_128SPS 128U

typedef struct {
    bool valid;
    int16_t raw;
    uint64_t timestamp_us;
} ads1118_sample_t;

typedef struct {
    runtime_component_t component;
    board_spi_bus_t spi_bus;
    uint8_t cs_pin;
    uint16_t sample_rate_sps;
    ads1118_sample_t sample;
} ads1118_t;

void ads1118_init(ads1118_t* dev, board_spi_bus_t spi_bus, uint8_t cs_pin);
const ads1118_sample_t* ads1118_get_sample(const ads1118_t* dev);
void ads1118_service_step(runtime_component_t* comp, uint64_t timestamp_us);

#ifdef __cplusplus
}
#endif
