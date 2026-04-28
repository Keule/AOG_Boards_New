#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "byte_ring_buffer.h"
#include "runtime_component.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UM980_DEFAULT_BAUDRATE 921600U

typedef enum {
    GNSS_UM980_PRIMARY = 0,
    GNSS_UM980_SECONDARY
} gnss_um980_role_t;

typedef struct {
    bool valid;
    bool has_fix;
    double latitude_deg;
    double longitude_deg;
    float altitude_m;
    float speed_knots;
    float course_deg;
    float sigma_lat_m;
    float sigma_lon_m;
    float sigma_alt_m;
    uint64_t timestamp_ms;
} gnss_snapshot_t;

typedef struct {
    gnss_um980_role_t role;
    uint32_t baud_rate;
    byte_ring_buffer_t* rx_buffer;
    byte_ring_buffer_t* tx_buffer;
    gnss_snapshot_t snapshot;
    uint32_t consumed_bytes;
    char line_buffer[128];
    size_t line_length;
    runtime_component_t component;
} gnss_um980_t;

void gnss_um980_init(gnss_um980_t* instance, gnss_um980_role_t role, byte_ring_buffer_t* rx_buffer, byte_ring_buffer_t* tx_buffer);
size_t gnss_um980_feed_rx(gnss_um980_t* instance, const uint8_t* data, size_t length);
void gnss_um980_consume_rx(gnss_um980_t* instance);
bool gnss_um980_get_snapshot(const gnss_um980_t* instance, gnss_snapshot_t* out_snapshot);
runtime_component_t* gnss_um980_component(gnss_um980_t* instance);

#ifdef __cplusplus
}
#endif
