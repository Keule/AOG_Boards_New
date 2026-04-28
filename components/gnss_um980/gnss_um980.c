#include "gnss_um980.h"

#include <string.h>

#include "protocol_nmea.h"

static void gnss_um980_component_step(runtime_component_t* component)
{
    gnss_um980_t* instance = 0;
    if (component == 0) {
        return;
    }

    instance = (gnss_um980_t*)component->user_data;
    gnss_um980_consume_rx(instance);
}

static void gnss_um980_handle_nmea_line(gnss_um980_t* instance, const char* line)
{
    nmea_solution_t solution;

    memset(&solution, 0, sizeof(solution));
    solution.has_fix = instance->snapshot.has_fix;
    solution.latitude_deg = instance->snapshot.latitude_deg;
    solution.longitude_deg = instance->snapshot.longitude_deg;
    solution.altitude_m = instance->snapshot.altitude_m;
    solution.speed_knots = instance->snapshot.speed_knots;
    solution.course_deg = instance->snapshot.course_deg;
    solution.sigma_lat_m = instance->snapshot.sigma_lat_m;
    solution.sigma_lon_m = instance->snapshot.sigma_lon_m;
    solution.sigma_alt_m = instance->snapshot.sigma_alt_m;

    if (!protocol_nmea_parse_line(line, &solution)) {
        return;
    }

    instance->snapshot.valid = true;
    instance->snapshot.has_fix = solution.has_fix;
    instance->snapshot.latitude_deg = solution.latitude_deg;
    instance->snapshot.longitude_deg = solution.longitude_deg;
    instance->snapshot.altitude_m = solution.altitude_m;
    instance->snapshot.speed_knots = solution.speed_knots;
    instance->snapshot.course_deg = solution.course_deg;
    instance->snapshot.sigma_lat_m = solution.sigma_lat_m;
    instance->snapshot.sigma_lon_m = solution.sigma_lon_m;
    instance->snapshot.sigma_alt_m = solution.sigma_alt_m;
    instance->snapshot.timestamp_ms += 10;
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
    uint8_t byte = 0;

    if (instance == 0 || instance->rx_buffer == 0) {
        return;
    }

    while (byte_ring_buffer_pop(instance->rx_buffer, &byte, 1) == 1) {
        instance->consumed_bytes++;

        if (byte == '\n' || byte == '\r') {
            if (instance->line_length > 0) {
                instance->line_buffer[instance->line_length] = '\0';
                gnss_um980_handle_nmea_line(instance, instance->line_buffer);
                instance->line_length = 0;
            }
            continue;
        }

        if (instance->line_length + 1U < sizeof(instance->line_buffer)) {
            instance->line_buffer[instance->line_length++] = (char)byte;
        } else {
            instance->line_length = 0;
        }
    }
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
