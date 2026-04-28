#include "transport_udp.h"

int transport_udp_init(void)
{
    return 0;
}

int transport_udp_send(const uint8_t* data, size_t length, size_t* sent)
{
    (void)data;
    if (sent != 0) {
        *sent = length;
    }
    return 0;
}

int transport_udp_receive(uint8_t* out_data, size_t max_length, size_t* received)
{
    (void)out_data;
    (void)max_length;
    if (received != 0) {
        *received = 0;
    }
    return 0;
}
