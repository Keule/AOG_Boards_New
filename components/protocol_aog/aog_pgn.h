#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <float.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * AOG PGN Data Structures — NAV-AOG-001 Nacharbeit
 *
 * Supports PGN 214 (consolidated GPS+Heading output), PGN 200/201 (legacy),
 * PGN 202/203 (Discovery), PGN 253/254 (Hello).
 *
 * All multi-byte fields are little-endian unless noted.
 * ======================================================================== */

/* ---- Sentinel Values for Invalid/Missing Data ---- */

#ifndef AOG_SENTINEL_DOUBLE
#define AOG_SENTINEL_DOUBLE    (1.7976931348623157e+308)  /* double max */
#endif

#ifndef AOG_SENTINEL_FLOAT
#define AOG_SENTINEL_FLOAT     FLT_MAX                      /* float max */
#endif

#ifndef AOG_SENTINEL_U16
#define AOG_SENTINEL_U16       0xFFFF
#endif

#ifndef AOG_SENTINEL_I16
#define AOG_SENTINEL_I16       0x7FFF
#endif

#ifndef AOG_SENTINEL_U8
#define AOG_SENTINEL_U8        0xFF
#endif

/* ========================================================================
 * PGN 214 — Consolidated GPS + Heading Output (AOG v5)
 *
 * Payload layout (51 bytes, all little-endian):
 *   Offset  Size  Type   Field
 *   0       8     f64    longitude (decimal degrees, E positive)
 *   8       8     f64    latitude  (decimal degrees, N positive)
 *   16      4     f32    heading_dual  (degrees, 0=N, clockwise, from dual antenna)
 *   20      4     f32    heading_true  (degrees, course over ground from RMC)
 *   24      4     f32    speed (km/h)
 *   28      4     f32    roll  (degrees)
 *   32      4     f32    altitude (meters)
 *   36      2     u16    satellites
 *   38      1     u8     fix_quality (0=none, 1=GPS, 2=DGPS, 4=RTK fix, 5=RTK float)
 *   39      2     u16    hdop (×0.01, e.g., hdop=1.5 → 150)
 *   41      2     u16    age (×0.01, correction age in seconds)
 *   43      2     u16    imu_heading (×0.1 degrees)
 *   45      2     i16    imu_roll (×0.1 degrees)
 *   47      2     i16    imu_pitch (degrees, not scaled)
 *   49      2     i16    imu_yaw_rate (degrees/s, not scaled)
 * ======================================================================== */

#define AOG_PGN214_DATA_SIZE   51

typedef struct {
    double   longitude;         /* decimal degrees, E positive, W negative */
    double   latitude;          /* decimal degrees, N positive, S negative */
    float    heading_dual;      /* degrees (0-360), dual antenna heading */
    float    heading_true;      /* degrees (0-360), course over ground */
    float    speed_kmh;         /* km/h */
    float    roll;              /* degrees */
    float    altitude;          /* meters above geoid */
    uint16_t satellites;        /* satellites in use */
    uint8_t  fix_quality;       /* 0=none, 1=GPS, 2=DGPS, 4=RTK_FIX, 5=RTK_FLOAT */
    uint16_t hdop_x100;         /* hdop × 100 (e.g., 1.5 → 150) */
    uint16_t age_x100;          /* correction age × 100 (seconds) */
    uint16_t imu_heading_x10;   /* imu heading × 10 (degrees) */
    int16_t  imu_roll_x10;      /* imu roll × 10 (degrees) */
    int16_t  imu_pitch;         /* imu pitch (degrees) */
    int16_t  imu_yaw_rate;      /* imu yaw rate (degrees/s) */
} aog_pgn214_t;

/* ---- AOG Fix Quality (for PGN 214 fix_quality byte) ---- */

#define AOG_FIX_NONE           0   /* No fix */
#define AOG_FIX_GPS            1   /* Autonomous GPS */
#define AOG_FIX_DGPS           2   /* Differential GPS */
#define AOG_FIX_RTK_FIX        4   /* RTK Fixed */
#define AOG_FIX_RTK_FLOAT      5   /* RTK Float */

/* ========================================================================
 * PGN 254 — Hello/Discovery Response (legacy, still supported)
 * ======================================================================== */

#define AOG_HELLO_DATA_SIZE  7

typedef struct {
    uint8_t  ip[4];           /* source IP address */
    uint16_t port;            /* source port (host byte order) */
    uint8_t  subnet_index;    /* subnet mask index */
} aog_hello_t;

/* ========================================================================
 * PGN 203 — Scan Reply (module capabilities)
 *
 * Payload layout (variable, minimum 3 bytes):
 *   [0]    : source address of responding module
 *   [1..2] : number of PGNs supported (u16 LE)
 *   [3..]  : PGN numbers (u16 LE each)
 * ======================================================================== */

#define AOG_SCAN_REPLY_MIN_SIZE  3
#define AOG_SCAN_REPLY_MAX_PGNS  8

typedef struct {
    uint8_t  src;
    uint8_t  module_type;     /* 0=GPS, 1=IMU, 2=Steering */
    uint8_t  pgn_count;
    uint16_t pgns[AOG_SCAN_REPLY_MAX_PGNS];
} aog_scan_reply_t;

/* ========================================================================
 * PGN 200 — GPS Position Output (legacy)
 * ======================================================================== */

#define AOG_POSITION_DATA_SIZE  18

typedef struct {
    uint8_t fix;
    uint8_t num_sats;
    double  latitude;
    double  longitude;
} aog_position_t;

/* ========================================================================
 * PGN 201 — Heading Output (legacy)
 * ======================================================================== */

#define AOG_HEADING_DATA_SIZE  16

typedef struct {
    double heading;    /* radians */
    double roll;       /* radians */
} aog_heading_t;

/* ========================================================================
 * PGN 252 — Steering (from AgOpenGPS to module)
 * ======================================================================== */

#define AOG_PGN_STEER_INPUT       252
#define AOG_PGN_STEER_STATUS      252
#define AOG_STEER_DATA_SIZE       8

typedef struct {
    float speed_ms;
    float steer_angle_deg;
} aog_steer_input_t;

#define AOG_STEER_STATUS_DATA_SIZE   8

typedef struct {
    float steer_angle_actual_deg;
    uint8_t status;
    uint8_t flags;
} aog_steer_status_t;

/* PGN 253 - Hello Request (from AgOpenGPS) */
#ifndef AOG_PGN_HELLO_REQUEST
#define AOG_PGN_HELLO_REQUEST   253
#endif

/* ========================================================================
 * PGN Encode/Decode Functions
 * ======================================================================== */

/* ---- PGN 214 ---- */

/* Encode PGN 214 payload into data buffer. Buffer must have AOG_PGN214_DATA_SIZE bytes.
 * Returns AOG_PGN214_DATA_SIZE. */
uint8_t aog_pgn_encode_pgn214(uint8_t* buffer, const aog_pgn214_t* data);

/* Decode PGN 214 payload from data buffer.
 * Returns true if data_length >= AOG_PGN214_DATA_SIZE. */
bool aog_pgn_decode_pgn214(const uint8_t* data, uint8_t data_length, aog_pgn214_t* out);

/* Fill PGN 214 struct with sentinel values (all fields = invalid). */
void aog_pgn214_set_sentinels(aog_pgn214_t* data);

/* Map gnss_fix_quality_t to AOG fix_quality byte.
 * Returns AOG_FIX_NONE (0) for invalid/unknown fix types. */
uint8_t aog_fix_quality_to_aog(uint8_t gnss_fix_quality);

/* ---- PGN 254: Hello ---- */

uint8_t aog_pgn_encode_hello(uint8_t* buffer, const aog_hello_t* hello);
bool aog_pgn_decode_hello(const uint8_t* data, uint8_t data_length, aog_hello_t* hello);

/* ---- PGN 203: Scan Reply ---- */

/* Encode scan reply payload. Returns total payload size. */
uint8_t aog_pgn_encode_scan_reply(uint8_t* buffer, const aog_scan_reply_t* reply);

/* ---- PGN 200: Position (legacy) ---- */

uint8_t aog_pgn_encode_position(uint8_t* buffer, const aog_position_t* pos);
bool aog_pgn_decode_position(const uint8_t* data, uint8_t data_length, aog_position_t* pos);

/* ---- PGN 201: Heading (legacy) ---- */

uint8_t aog_pgn_encode_heading(uint8_t* buffer, const aog_heading_t* hdg);
bool aog_pgn_decode_heading(const uint8_t* data, uint8_t data_length, aog_heading_t* hdg);

#ifdef __cplusplus
}
#endif
