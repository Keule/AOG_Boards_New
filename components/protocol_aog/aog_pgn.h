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

/* PGN 252 - Steering Input (from AgOpenGPS to module)
 * Data layout (8 bytes):
 *   [0..3] : vehicle speed in m/s  (float, little-endian)
 *   [4..7] : steer angle setpoint  (float, little-endian, degrees)
 */
#define AOG_PGN_STEER_INPUT       252
#define AOG_PGN_STEER_STATUS      252   /* Same PGN, output direction */
#define AOG_STEER_DATA_SIZE       8

typedef struct {
    float speed_ms;            /* vehicle speed m/s */
    float steer_angle_deg;     /* steering angle setpoint degrees */
} aog_steer_input_t;

/* PGN 252 - Steering Status (module -> AgOpenGPS output)
 * Data layout (8 bytes):
 *   [0..3] : actual steer angle    (float, little-endian, degrees)
 *   [4]    : status byte  (0=disabled, 1=manual, 2=auto)
 *   [5]    : flags (reserved)
 *   [6..7] : padding
 */
#define AOG_STEER_STATUS_DATA_SIZE   8

typedef struct {
    float steer_angle_actual_deg;  /* actual steering angle */
    uint8_t status;                /* 0=disabled, 1=manual, 2=auto */
    uint8_t flags;                 /* reserved */
} aog_steer_status_t;

/* PGN 253 - Hello Request (from AgOpenGPS) */
#ifndef AOG_PGN_HELLO_REQUEST
#define AOG_PGN_HELLO_REQUEST   253
#endif

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
