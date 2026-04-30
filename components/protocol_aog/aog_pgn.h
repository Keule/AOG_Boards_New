#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <float.h>
#include <limits.h>

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

/* ========================================================================
 * PGN 214 - Combined GPS + Heading Output (NAV-AOG-001)
 *
 * Single PGN carrying position, heading, speed, and IMU data.
 * Replaces separate PGN 200 + PGN 201 for AOG 6.x+ compatibility.
 *
 * Data layout (51 bytes, little-endian):
 *   [ 0.. 7] : longitude       float64 LE
 *   [ 8..15] : latitude        float64 LE
 *   [16..19] : heading_dual    float32 LE  (dual-antenna heading, radians)
 *   [20..23] : heading_true    float32 LE  (true/course heading, radians)
 *   [24..27] : speed           float32 LE  (m/s)
 *   [28..31] : roll            float32 LE  (radians)
 *   [32..35] : altitude        float32 LE  (meters)
 *   [36..37] : satellites      uint16 LE
 *   [38]    : fix_quality     uint8
 *   [39..40] : hdop            uint16 LE   (scale 0.01 → value = hdop * 100)
 *   [41..42] : age             uint16 LE   (scale 0.01 → value = age_s * 100)
 *   [43..44] : imu_heading     uint16 LE   (scale 0.1  → value = deg * 10)
 *   [45..46] : imu_roll        int16  LE   (scale 0.1  → value = deg * 10)
 *   [47..48] : imu_pitch       int16  LE   (raw degrees)
 *   [49..50] : imu_yaw_rate    int16  LE   (raw deg/s)
 * ======================================================================== */

#define AOG_PGN_214              214
#define AOG_PGN214_DATA_SIZE     51

/* ---- Sentinel values for missing/invalid data ---- */
#define AOG_PGN214_SENTINEL_DOUBLE   DBL_MAX   /* invalid lat/lon */
#define AOG_PGN214_SENTINEL_FLOAT    FLT_MAX   /* invalid heading/speed/roll/alt */
#define AOG_PGN214_SENTINEL_UINT16   UINT16_MAX  /* invalid uint16 fields */
#define AOG_PGN214_SENTINEL_INT16    INT16_MAX   /* invalid int16 fields */
#define AOG_PGN214_SENTINEL_FIX      0           /* no fix */

typedef struct {
    /* ---- Position (from GNSS primary) ---- */
    double  longitude;        /* decimal degrees, SENTINEL_DOUBLE if invalid */
    double  latitude;         /* decimal degrees, SENTINEL_DOUBLE if invalid */
    float   altitude;         /* meters above geoid, SENTINEL_FLOAT if invalid */

    /* ---- GNSS Heading (from dual-antenna) ---- */
    float   heading_dual;     /* radians, SENTINEL_FLOAT if no dual heading */
    float   heading_true;     /* radians, SENTINEL_FLOAT if no course over ground */

    /* ---- Motion ---- */
    float   speed;            /* m/s, SENTINEL_FLOAT if invalid */
    float   roll;             /* radians, SENTINEL_FLOAT if invalid */

    /* ---- Fix info ---- */
    uint8_t fix_quality;      /* 0=none, 1=2D, 2=3D, 3=RTK fix, 4=RTK float */
    uint16_t satellites;      /* number of satellites, SENTINEL_UINT16 if invalid */
    uint16_t hdop_x100;       /* HDOP * 100 (scale 0.01), SENTINEL_UINT16 if invalid */

    /* ---- Correction age ---- */
    uint16_t age_x100;        /* correction age seconds * 100, SENTINEL_UINT16 if N/A */

    /* ---- IMU data (from BNO085, Future Work) ---- */
    uint16_t imu_heading_x10; /* heading degrees * 10, SENTINEL_UINT16 if no IMU */
    int16_t  imu_roll_x10;    /* roll degrees * 10, SENTINEL_INT16 if no IMU */
    int16_t  imu_pitch;       /* pitch degrees raw, SENTINEL_INT16 if no IMU */
    int16_t  imu_yaw_rate;    /* yaw rate deg/s raw, SENTINEL_INT16 if no IMU */
} aog_pgn214_t;

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

/* Encode PGN 214 into data buffer. Buffer must have AOG_PGN214_DATA_SIZE bytes.
 * Returns AOG_PGN214_DATA_SIZE.
 * Maps sentinel values to wire format. */
uint8_t aog_pgn_encode_214(uint8_t* buffer, const aog_pgn214_t* data);

/* Decode PGN 214 from data buffer.
 * Returns true if data_length >= AOG_PGN214_DATA_SIZE.
 * Restores sentinel values for invalid fields. */
bool aog_pgn_decode_214(const uint8_t* data, uint8_t data_length, aog_pgn214_t* out);

#ifdef __cplusplus
}
#endif
