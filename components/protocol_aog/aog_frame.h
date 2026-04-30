#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- AOG Frame Format (v5 — with Source Byte) ----
 *
 * [0x80][0x81][SRC][PGN_lo][PGN_hi][LEN][DATA_0]..[DATA_n-1][CRC]
 *
 * preamble  : 2 bytes (0x80, 0x81)
 * src       : 1 byte  = source address (0x05 = GPS, 0x06 = IMU, etc.)
 * pgn       : 2 bytes little-endian
 * length    : 1 byte  = data_length (payload only)
 * data      : length bytes
 * crc       : 1 byte  = sum(bytes[2..5+length]) mod 256
 *                     = (SRC + PGN_lo + PGN_hi + LEN + DATA[0..n-1]) % 256
 *
 * Total frame size = 7 + data_length
 *
 * Source Addresses:
 *   0x05 = GPS / Position module
 *   0x06 = IMU module
 *   0x07 = Steering module
 */

#define AOG_PREAMBLE_1          0x80
#define AOG_PREAMBLE_2          0x81

/* Source addresses (PGN-Verzeichnis) */
#define AOG_SRC_AOG             0x00   /* AgOpenGPS itself */
#define AOG_SRC_GPS             0x05   /* GPS / GNSS navigation module */
#define AOG_SRC_IMU             0x06   /* Inertial measurement unit */
#define AOG_SRC_STEER           0x07   /* Steering controller */

/* Module types (PGN-Verzeichnis — for Scan Reply payload) */
#define AOG_MODULE_TYPE_GPS     120    /* 0x78 — GPS module */
#define AOG_MODULE_TYPE_IMU     121    /* 0x79 — IMU module */
#define AOG_MODULE_TYPE_STEER   122    /* 0x7A — Steering module */

#define AOG_MAX_DATA_SIZE       80     /* increased for PGN 214 (51 bytes) + margin */
#define AOG_MAX_FRAME_SIZE      (7 + AOG_MAX_DATA_SIZE)

/* PGN numbers */
#define AOG_PGN_POSITION_OUT    200
#define AOG_PGN_HEADING_OUT     201
#define AOG_PGN_SCAN_REQUEST    202
#define AOG_PGN_SCAN_REPLY      203
#define AOG_PGN_214_OUT         214    /* 0xD6 — consolidated GPS+Heading */
#define AOG_PGN_HELLO_REQUEST   253
#define AOG_PGN_HELLO_RESPONSE  254

/* Parser states */
typedef enum {
    AOG_PARSE_IDLE = 0,
    AOG_PARSE_PREAMBLE,
    AOG_PARSE_SRC,
    AOG_PARSE_PGN,
    AOG_PARSE_LENGTH,
    AOG_PARSE_DATA,
    AOG_PARSE_CRC
} aog_parse_state_t;

/* Streaming frame parser */
typedef struct {
    aog_parse_state_t state;
    uint8_t  src;           /* source address */
    uint16_t pgn;
    uint8_t  length;        /* data length */
    uint8_t  data[AOG_MAX_DATA_SIZE];
    uint8_t  data_count;
    uint8_t  crc;
    bool     frame_ready;
    bool     crc_valid;
    bool     discovery_tolerant;  /* NAV-AOG-001-FINAL: tolerant CRC for Discovery PGNs */
} aog_parser_t;

/* CRC calculation: sum of bytes mod 256 */
uint8_t aog_crc_calculate(const uint8_t* data, size_t length);

/* Encode a v5 frame into buffer.
 * buffer must have at least AOG_MAX_FRAME_SIZE bytes.
 * src: source address (e.g., AOG_SRC_GPS)
 * Returns total frame length, or 0 on error (data too large). */
size_t aog_frame_encode(uint8_t* buffer, uint8_t src, uint16_t pgn,
                         const uint8_t* data, uint8_t data_length);

/* Verify CRC of a complete v5 frame in memory (STRICT).
 * frame includes preamble, src, pgn, length, data, and crc.
 * Returns true only if CRC matches exactly.
 * Use for core PGN output (PGN 214, 200, 201, 252). */
bool aog_frame_verify_crc(const uint8_t* frame, size_t frame_length);

/* Verify CRC in TOLERANT mode for Discovery frames.
 * Accepts frames where CRC matches OR CRC == 0x00 (known AgIO quirk).
 * Also accepts frames where CRC is off by exactly ±1 (wire noise).
 * Returns true if frame is acceptable under tolerant rules.
 * Use ONLY for Discovery PGNs: 202 (Scan Request), 253 (Hello Request). */
bool aog_frame_verify_crc_tolerant(const uint8_t* frame, size_t frame_length);

/* Check if a PGN is a Discovery PGN (tolerant CRC allowed). */
bool aog_pgn_is_discovery(uint16_t pgn);

/* Initialize parser. discovery_tolerant = true enables tolerant CRC for
 * Discovery PGNs (202, 253). Core PGNs (214, 200, 201) always use strict CRC. */
void aog_parser_init(aog_parser_t* parser);
void aog_parser_init_ex(aog_parser_t* parser, bool discovery_tolerant);

/* Feed one byte into the parser.
 * Returns true when a complete frame has been parsed.
 * After returning true, src/pgn/data/data_count/crc_valid are valid
 * and the parser is reset for the next frame. */
bool aog_parser_feed(aog_parser_t* parser, uint8_t byte);

#ifdef __cplusplus
}
#endif
