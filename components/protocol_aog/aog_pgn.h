#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- AOG PGN Data Structures ---- */

/* PGN 254 - Hello/Discovery Response
 * Sent by module in response to AOG Hello.
 * Data layout (7 bytes):
 *   [0..3] : source IP address (4 bytes)
 *   [4..5] : source port (uint16_t, little-endian)
 *   [6]    : subnet mask index
 */
#define AOG_HELLO_DATA_SIZE  7

typedef struct {
    uint8_t  ip[4];           /* source IP address */
    uint16_t port;            /* source port (host byte order) */
    uint8_t  subnet_index;    /* subnet mask index */
} aog_hello_t;

/* PGN 200 - GPS Position Output
 * Sent by module with current GPS position.
 * Data layout (18 bytes):
 *   [0]    : fix status (0=none, 1=2D, 2=3D, 3=RTK fix, 4=RTK float)
 *   [1]    : number of satellites
 *   [2..9] : latitude (double, little-endian)
 *   [10..17]: longitude (double, little-endian)
 */
#define AOG_POSITION_DATA_SIZE  18

typedef struct {
    uint8_t fix;
    uint8_t num_sats;
    double  latitude;
    double  longitude;
} aog_position_t;

/* PGN 201 - Heading Output
 * Sent by module with current heading.
 * Data layout (16 bytes):
 *   [0..7]  : heading in radians (double, little-endian)
 *   [8..15] : roll in radians (double, little-endian)
 */
#define AOG_HEADING_DATA_SIZE  16

typedef struct {
    double heading;    /* radians */
    double roll;       /* radians */
} aog_heading_t;

/* ---- PGN Encode/Decode ---- */

/* Encode Hello PGN into data buffer. Buffer must have AOG_HELLO_DATA_SIZE bytes.
 * Returns AOG_HELLO_DATA_SIZE. */
uint8_t aog_pgn_encode_hello(uint8_t* buffer, const aog_hello_t* hello);

/* Decode Hello PGN from data buffer.
 * Returns true if data_length >= AOG_HELLO_DATA_SIZE. */
bool aog_pgn_decode_hello(const uint8_t* data, uint8_t data_length, aog_hello_t* hello);

/* Encode Position PGN into data buffer. Buffer must have AOG_POSITION_DATA_SIZE bytes.
 * Returns AOG_POSITION_DATA_SIZE. */
uint8_t aog_pgn_encode_position(uint8_t* buffer, const aog_position_t* pos);

/* Decode Position PGN from data buffer.
 * Returns true if data_length >= AOG_POSITION_DATA_SIZE. */
bool aog_pgn_decode_position(const uint8_t* data, uint8_t data_length, aog_position_t* pos);

/* Encode Heading PGN into data buffer. Buffer must have AOG_HEADING_DATA_SIZE bytes.
 * Returns AOG_HEADING_DATA_SIZE. */
uint8_t aog_pgn_encode_heading(uint8_t* buffer, const aog_heading_t* hdg);

/* Decode Heading PGN from data buffer.
 * Returns true if data_length >= AOG_HEADING_DATA_SIZE. */
bool aog_pgn_decode_heading(const uint8_t* data, uint8_t data_length, aog_heading_t* hdg);

#ifdef __cplusplus
}
#endif
