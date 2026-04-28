#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "runtime_component.h"
#include "nmea_parser.h"
#include "snapshot_buffer.h"
#include "byte_ring_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- UM980 Receiver Instance ----
 *
 * Consumes raw NMEA bytes from an external RX source buffer.
 * Parses NMEA sentences and maintains latest position data.
 * Provides snapshots for downstream consumers (e.g., dual heading, AOG app).
 *
 * This component does NOT read UART or call HAL/Transport directly.
 * It only consumes bytes via its feed API or rx_source buffer.
 *
 * IMPORTANT: runtime_component_t MUST be the first field for safe casting. */
typedef struct {
    runtime_component_t component;    /* MUST be first */

    /* Identity */
    uint8_t     instance_id;    /* 0 = primary, 1 = secondary */
    const char* name;

    /* NMEA streaming parser */
    nmea_parser_t nmea_parser;

    /* RX data source (set by caller, NOT owned).
     * Typically points to transport_uart.rx_buffer. */
    byte_ring_buffer_t* rx_source;

    /* Latest parsed data */
    nmea_gga_t gga;
    nmea_rmc_t rmc;
    bool       gga_valid;
    bool       rmc_valid;

    /* Snapshot output for downstream consumers */
    snapshot_buffer_t position_snapshot;   /* nmea_gga_t */
    nmea_gga_t        position_storage;

    /* Statistics */
    uint32_t sentences_parsed;
    uint32_t sentences_error;
    uint32_t bytes_received;
} gnss_um980_t;

/* ---- API ---- */

/* Initialize a UM980 instance. */
void gnss_um980_init(gnss_um980_t* rx, uint8_t instance_id, const char* name);

/* Set the RX source buffer (e.g., transport_uart.rx_buffer).
 * The source is NOT owned by this component. */
void gnss_um980_set_rx_source(gnss_um980_t* rx, byte_ring_buffer_t* source);

/* Feed raw bytes into the receiver's NMEA parser (manual API).
 * Returns number of complete sentences parsed. */
uint32_t gnss_um980_feed(gnss_um980_t* rx, const uint8_t* data, size_t length);

/* Service step: consume bytes from rx_source, parse NMEA.
 * Called by the runtime service loop. */
void gnss_um980_service_step(runtime_component_t* comp, uint64_t timestamp_us);

/* Get pointer to latest GGA data. NULL if not valid. */
const nmea_gga_t* gnss_um980_get_gga(const gnss_um980_t* rx);

/* Get pointer to latest RMC data. NULL if not valid. */
const nmea_rmc_t* gnss_um980_get_rmc(const gnss_um980_t* rx);

/* Check if position data is valid. */
bool gnss_um980_has_fix(const gnss_um980_t* rx);

#ifdef __cplusplus
}
#endif
