/* ads1118.c — ADS1118 16-bit ADC component (skeleton).
 *
 * Reads raw ADC conversion at the configured sample rate via SPI.
 * The sample_rate config field is stored in the instance struct and
 * would be used to build the ADS1118 config word in a real implementation.
 *
 * SPI skeleton: sends zeros, receives zeros. Real implementation would
 * send the 24-bit ADS1118 config+read frame.
 */

#include "ads1118.h"
#include <string.h>

void ads1118_init(ads1118_t* adc, board_spi_bus_t bus, uint8_t cs_pin)
{
    if (adc == NULL) {
        return;
    }

    memset(adc, 0, sizeof(ads1118_t));
    adc->spi_bus      = bus;
    adc->cs_pin       = cs_pin;
    adc->sample_rate  = ADS1118_RATE_DEFAULT;  /* 128 SPS */

    /* Register service step callback */
    adc->component.service_step = ads1118_service_step;
}

void ads1118_set_sample_rate(ads1118_t* adc, uint8_t rate)
{
    if (adc == NULL) {
        return;
    }
    adc->sample_rate = rate;
}

void ads1118_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    (void)timestamp_us;
    ads1118_t* adc = (ads1118_t*)comp;
    if (adc == NULL) {
        return;
    }

    /* SPI read: send ADC config word, receive conversion result.
     * TODO: implement actual ADS1118 SPI protocol.
     *       ADS1118 requires 24-bit SPI frame with config word.
     *       Config word includes sample_rate field from adc->sample_rate.
     *
     * Skeleton: tx_buf[0..1] would contain the config register value
     * with sample_rate bits set according to adc->sample_rate. */
    uint8_t tx_buf[4];
    uint8_t rx_buf[4];
    memset(tx_buf, 0, sizeof(tx_buf));
    memset(rx_buf, 0, sizeof(rx_buf));

    /* In a real implementation, tx_buf would be:
     *   tx_buf[0] = (adc->sample_rate << 3) | ADS1118_CONFIG_BITS;
     *   tx_buf[1] = additional config bits;
     *   tx_buf[2] = 0x00; (start conversion on 3rd byte) */

    hal_spi_transfer(adc->spi_bus, adc->cs_pin, tx_buf, rx_buf, sizeof(tx_buf));
    adc->read_count++;

    /* Parse stub: real implementation would extract 16-bit result from rx_buf.
     * Example: adc->raw_value = ((uint16_t)rx_buf[0] << 8) | rx_buf[1]; */
    adc->raw_value  = 0;
    adc->data_valid = true;
}

uint16_t ads1118_get_raw(const ads1118_t* adc)
{
    if (adc == NULL) {
        return 0;
    }
    return adc->raw_value;
}

bool ads1118_has_data(const ads1118_t* adc)
{
    if (adc == NULL) {
        return false;
    }
    return adc->data_valid;
}
