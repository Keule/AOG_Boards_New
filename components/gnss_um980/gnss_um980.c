#include "gnss_um980.h"

#include "protocol_nmea.h"
#include "transport_uart.h"
#include "hal_uart.h"

static gnss_um980_receiver_data_t s_primary;
static gnss_um980_receiver_data_t s_secondary;
static nmea_parser_t s_parser_primary;
static nmea_parser_t s_parser_secondary;

static void update_from_message(gnss_um980_receiver_data_t* out, const nmea_message_t* msg)
{
    if (out == 0 || msg == 0 || msg->status != NMEA_PARSE_OK) {
        return;
    }

    switch (msg->type) {
        case NMEA_SENTENCE_GGA:
            out->has_gga = true;
            out->latitude_e7 = (int32_t)(msg->data.gga.latitude_deg * 10000000.0);
            out->longitude_e7 = (int32_t)(msg->data.gga.longitude_deg * 10000000.0);
            out->altitude_mm = (int32_t)(msg->data.gga.altitude_m * 1000.0);
            out->valid = true;
            break;
        case NMEA_SENTENCE_RMC:
            out->has_rmc = true;
            break;
        case NMEA_SENTENCE_GST:
            out->has_gst = true;
            break;
        case NMEA_SENTENCE_GSV:
            out->has_gsv = true;
            break;
        case NMEA_SENTENCE_GSA:
            out->has_gsa = true;
            break;
        default:
            break;
    }
}

static void read_receiver(hal_uart_port_t port, nmea_parser_t* parser, gnss_um980_receiver_data_t* data)
{
    uint8_t buffer[256];
    size_t read_count = 0;
    size_t i = 0;

    if (transport_uart_read_nonblocking(port, buffer, sizeof(buffer), &read_count) != 0) {
        return;
    }

    for (i = 0; i < read_count; i++) {
        nmea_message_t msg;
        if (nmea_parser_feed(parser, (char)buffer[i], &msg)) {
            update_from_message(data, &msg);
        }
    }
}

static void gnss_um980_fast_input(runtime_component_t* component, const fast_cycle_context_t* ctx)
{
    (void)component;
    (void)ctx;

    read_receiver(HAL_UART_PORT_GNSS_PRIMARY, &s_parser_primary, &s_primary);
    read_receiver(HAL_UART_PORT_GNSS_SECONDARY, &s_parser_secondary, &s_secondary);
}

static runtime_component_t s_component = {
    .name = "gnss_um980",
    .user_data = 0,
    .fast_input = gnss_um980_fast_input,
    .fast_process = 0,
    .fast_output = 0,
};

int gnss_um980_init(void)
{
    nmea_parser_init(&s_parser_primary);
    nmea_parser_init(&s_parser_secondary);
    s_primary = (gnss_um980_receiver_data_t){0};
    s_secondary = (gnss_um980_receiver_data_t){0};

    return transport_uart_init();
}

runtime_component_t* gnss_um980_component(void)
{
    return &s_component;
}

const gnss_um980_receiver_data_t* gnss_um980_primary(void)
{
    return &s_primary;
}

const gnss_um980_receiver_data_t* gnss_um980_secondary(void)
{
    return &s_secondary;
}
