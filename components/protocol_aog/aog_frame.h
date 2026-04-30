#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- AOG Frame Format ----
 *
 * [0x80][0x81][length][PGN_lo][PGN_hi][data_0]..[data_n-1][crc]
 *
 * preamble  : 2 bytes (0x80, 0x81)
 * length    : 1 byte  = 2 + data_length  (PGN bytes + data bytes)
 * pgn       : 2 bytes little-endian
 * data      : length - 2 bytes
 * crc       : 1 byte  = XOR(length, PGN_lo, PGN_hi, data_0, ..., data_n-1)
 *
 * Total frame size = 6 + data_length
 */

#define AOG_PREAMBLE_1          0x80
#define AOG_PREAMBLE_2          0x81

#define AOG_MAX_DATA_SIZE       64
#define AOG_MAX_FRAME_SIZE      (6 + AOG_MAX_DATA_SIZE)  /* preamble(2) + length(1) + pgn(2) + data + crc(1) */

/* PGN numbers */
#define AOG_PGN_POSITION_OUT    200
#define AOG_PGN_HEADING_OUT     201
#define AOG_PGN_214              214
#define AOG_PGN_HELLO_RESPONSE  254

/* Parser states */
typedef enum {
    AOG_PARSE_IDLE = 0,
    AOG_PARSE_PREAMBLE,
    AOG_PARSE_LENGTH,
    AOG_PARSE_PGN,
    AOG_PARSE_DATA,
    AOG_PARSE_CRC
} aog_parse_state_t;

/* Streaming frame parser */
typedef struct {
    aog_parse_state_t state;
    uint8_t length;
    uint16_t pgn;
    uint8_t data[AOG_MAX_DATA_SIZE];
    uint8_t data_count;
    uint8_t crc;
    bool frame_ready;
    bool crc_valid;
} aog_parser_t;

/* CRC calculation: XOR of all bytes */
uint8_t aog_crc_calculate(const uint8_t* data, size_t length);

/* Encode a frame into buffer.
 * buffer must have at least AOG_MAX_FRAME_SIZE bytes.
 * Returns total frame length, or 0 on error (data too large). */
size_t aog_frame_encode(uint8_t* buffer, uint16_t pgn, const uint8_t* data, uint8_t data_length);

/* Verify CRC of a complete frame in memory.
 * frame includes preamble, length, pgn, data, and crc.
 * Returns true if CRC is valid. */
bool aog_frame_verify_crc(const uint8_t* frame, size_t frame_length);

/* Streaming parser */
void aog_parser_init(aog_parser_t* parser);

/* Feed one byte into the parser.
 * Returns true when a complete frame has been parsed.
 * After returning true, pgn/data/data_count/crc_valid are valid
 * and the parser is reset for the next frame. */
bool aog_parser_feed(aog_parser_t* parser, uint8_t byte);

#ifdef __cplusplus
}
#endif
