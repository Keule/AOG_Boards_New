#pragma once

/* ads1118.h — ADS1118 16-bit ADC component (skeleton).
 *
 * SPI-based 16-bit ADC. Reads raw conversion at configurable sample rate.
 * No calibration — raw output only.
 *
 * IMPORTANT: runtime_component_t MUST be the first field for safe casting. */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "runtime_component.h"
#include "hal_spi.h"
#include "board_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- ADS1118 Sample Rate Configuration ---- */

#define ADS1118_RATE_8SPS      0x00
#define ADS1118_RATE_16SPS     0x01
#define ADS1118_RATE_32SPS     0x02
#define ADS1118_RATE_64SPS     0x03
#define ADS1118_RATE_128SPS    0x04
#define ADS1118_RATE_250SPS    0x05
#define ADS1118_RATE_475SPS    0x06
#define ADS1118_RATE_860SPS    0x07

#define ADS1118_RATE_DEFAULT   ADS1118_RATE_128SPS

/* ---- ADS1118 ADC Instance ---- */

typedef struct {
    runtime_component_t component;    /* MUST be first */

    board_spi_bus_t spi_bus;
    uint8_t cs_pin;

    /* Configuration */
    uint8_t sample_rate;      /**< Sample rate register value (ADS1118_RATE_xxx) */

    /* Data output */
    uint16_t raw_value;
    bool data_valid;
    uint32_t read_count;
} ads1118_t;

/* ---- API ---- */

/** Initialize ADS1118 instance on given SPI bus and CS pin.
 *  Sets default sample rate to ADS1118_RATE_128SPS. */
void ads1118_init(ads1118_t* adc, board_spi_bus_t bus, uint8_t cs_pin);

/** Set the sample rate configuration.
 *  rate: one of ADS1118_RATE_xxx constants.
 *  Must be called before first service_step or during init. */
void ads1118_set_sample_rate(ads1118_t* adc, uint8_t rate);

/** Service step: SPI read, parse stub.
 * Called by the runtime service loop. */
void ads1118_service_step(runtime_component_t* comp, uint64_t timestamp_us);

/** Get last raw ADC value. */
uint16_t ads1118_get_raw(const ads1118_t* adc);

/** Check if data is valid. */
bool ads1118_has_data(const ads1118_t* adc);

#ifdef __cplusplus
}
#endif
