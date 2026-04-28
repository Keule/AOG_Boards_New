#include "gnss_um980.h"

#include <string.h>

static void gnss_um980_component_step(runtime_component_t* component)
{
    gnss_um980_t* instance = 0;
    if (component == 0) {
        return;
    }

    instance = (gnss_um980_t*)component->user_data;
    gnss_um980_consume_rx(instance);
}

void gnss_um980_init(gnss_um980_t* instance, gnss_um980_role_t role, byte_ring_buffer_t* rx_buffer, byte_ring_buffer_t* tx_buffer)
{
    if (instance == 0) {
        return;
    }

    memset(instance, 0, sizeof(*instance));
    instance->role = role;
    instance->baud_rate = UM980_DEFAULT_BAUDRATE;
    instance->rx_buffer = rx_buffer;
    instance->tx_buffer = tx_buffer;
    instance->component.name = (role == GNSS_UM980_PRIMARY) ? "gnss_um980_primary" : "gnss_um980_secondary";
    instance->component.user_data = instance;
    instance->component.step = gnss_um980_component_step;
}

size_t gnss_um980_feed_rx(gnss_um980_t* instance, const uint8_t* data, size_t length)
{
    if (instance == 0 || instance->rx_buffer == 0) {
        return 0;
    }

    return byte_ring_buffer_push(instance->rx_buffer, data, length);
}

void gnss_um980_consume_rx(gnss_um980_t* instance)
{
    uint8_t temp[64];
    size_t read_len = 0;

    if (instance == 0 || instance->rx_buffer == 0) {
        return;
    }

    read_len = byte_ring_buffer_pop(instance->rx_buffer, temp, sizeof(temp));
    if (read_len == 0) {
        return;
    }

    instance->consumed_bytes += (uint32_t)read_len;
    instance->snapshot.valid = true;
    instance->snapshot.latitude_deg = (instance->role == GNSS_UM980_PRIMARY) ? 50.0 : 50.0001;
    instance->snapshot.longitude_deg = (instance->role == GNSS_UM980_PRIMARY) ? 8.0 : 8.0001;
    instance->snapshot.altitude_m = 100.0f;
    instance->snapshot.timestamp_ms += 10;
}

bool gnss_um980_get_snapshot(const gnss_um980_t* instance, gnss_snapshot_t* out_snapshot)
{
    if (instance == 0 || out_snapshot == 0 || !instance->snapshot.valid) {
        return false;
    }

    *out_snapshot = instance->snapshot;
    return true;
}

runtime_component_t* gnss_um980_component(gnss_um980_t* instance)
{
    if (instance == 0) {
        return 0;
    }

    return &instance->component;
}
