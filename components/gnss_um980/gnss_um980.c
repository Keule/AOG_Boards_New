#include "gnss_um980.h"
#include <string.h>

void gnss_um980_init(gnss_um980_t* rx, uint8_t instance_id, const char* name)
{
    if (rx == NULL) {
        return;
    }

    memset(rx, 0, sizeof(gnss_um980_t));
    rx->instance_id = instance_id;
    rx->name = name;

    nmea_parser_init(&rx->nmea_parser);
    snapshot_buffer_init(&rx->position_snapshot, &rx->position_storage, sizeof(nmea_gga_t));

    /* Register service step callback */
    rx->component.service_step = gnss_um980_service_step;
}

void gnss_um980_set_rx_source(gnss_um980_t* rx, byte_ring_buffer_t* source)
{
    if (rx == NULL) {
        return;
    }
    rx->rx_source = source;
}

uint32_t gnss_um980_feed(gnss_um980_t* rx, const uint8_t* data, size_t length)
{
    if (rx == NULL || data == NULL || length == 0) {
        return 0;
    }

    uint32_t sentences_parsed = 0;
    rx->bytes_received += (uint32_t)length;

    for (size_t i = 0; i < length; i++) {
        nmea_result_t result = nmea_parser_feed(&rx->nmea_parser, data[i]);

        if (result == NMEA_RESULT_VALID) {
            sentences_parsed++;

            switch (rx->nmea_parser.type) {
            case NMEA_SENTENCE_GGA:
                rx->gga = rx->nmea_parser.data.gga;
                rx->gga_valid = true;
                snapshot_buffer_set(&rx->position_snapshot, &rx->gga);
                break;
            case NMEA_SENTENCE_RMC:
                rx->rmc = rx->nmea_parser.data.rmc;
                rx->rmc_valid = true;
                break;
            default:
                /* GSV, GSA, GST: parsed but not individually stored in v1 */
                break;
            }
        } else if (result == NMEA_RESULT_INVALID_CHECKSUM) {
            rx->sentences_error++;
        }
    }

    rx->sentences_parsed += sentences_parsed;
    return sentences_parsed;
}

void gnss_um980_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    (void)timestamp_us;
    gnss_um980_t* rx = (gnss_um980_t*)comp;
    if (rx == NULL || rx->rx_source == NULL) {
        return;
    }

    /* Consume bytes from RX source buffer */
    uint8_t tmp[64];
    size_t available = byte_ring_buffer_available(rx->rx_source);
    if (available == 0) {
        return;
    }

    size_t to_read = available > sizeof(tmp) ? sizeof(tmp) : available;
    size_t pulled = byte_ring_buffer_read(rx->rx_source, tmp, to_read);
    if (pulled > 0) {
        gnss_um980_feed(rx, tmp, pulled);
    }
}

const nmea_gga_t* gnss_um980_get_gga(const gnss_um980_t* rx)
{
    if (rx == NULL || !rx->gga_valid) {
        return NULL;
    }
    return &rx->gga;
}

const nmea_rmc_t* gnss_um980_get_rmc(const gnss_um980_t* rx)
{
    if (rx == NULL || !rx->rmc_valid) {
        return NULL;
    }
    return &rx->rmc;
}

bool gnss_um980_has_fix(const gnss_um980_t* rx)
{
    if (rx == NULL || !rx->gga_valid) {
        return false;
    }
    return rx->gga.fix_quality > 0;
}
