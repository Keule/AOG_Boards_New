#include "aog_frame.h"
#include <string.h>

/* ---- CRC: sum of bytes mod 256 ---- */

uint8_t aog_crc_calculate(const uint8_t* data, size_t length)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < length; i++) {
        sum += data[i];
    }
    return (uint8_t)(sum & 0xFF);
}

/* ---- v5 Frame Encoder ----
 * [0x80][0x81][SRC][PGN_lo][PGN_hi][LEN][DATA...][CRC]
 *
 * CRC is ALWAYS strict on TX — no tolerant encoding.
 * This ensures all outgoing frames are byte-exact.
 */

size_t aog_frame_encode(uint8_t* buffer, uint8_t src, uint16_t pgn,
                         const uint8_t* data, uint8_t data_length)
{
    if (data_length > AOG_MAX_DATA_SIZE) {
        return 0;
    }

    buffer[0] = AOG_PREAMBLE_1;
    buffer[1] = AOG_PREAMBLE_2;
    buffer[2] = src;                                  /* source address */
    buffer[3] = (uint8_t)(pgn & 0xFF);               /* PGN low byte */
    buffer[4] = (uint8_t)((pgn >> 8) & 0xFF);        /* PGN high byte */
    buffer[5] = data_length;                          /* payload length */

    if (data_length > 0 && data != NULL) {
        memcpy(&buffer[6], data, data_length);
    }

    /* CRC = sum(SRC + PGN_lo + PGN_hi + LEN + DATA[0..len-1]) mod 256 */
    uint8_t crc = aog_crc_calculate(&buffer[2], 4 + data_length);
    buffer[6 + data_length] = crc;

    return 7 + data_length;
}

/* ---- CRC Verification: STRICT ----
 * Returns true only if CRC matches exactly.
 * Use for core PGN output (PGN 214, 200, 201, 252).
 */

bool aog_frame_verify_crc(const uint8_t* frame, size_t frame_length)
{
    /* Minimum frame: preamble(2) + src(1) + pgn(2) + len(1) + crc(1) = 7 */
    if (frame_length < 7) {
        return false;
    }

    if (frame[0] != AOG_PREAMBLE_1 || frame[1] != AOG_PREAMBLE_2) {
        return false;
    }

    uint8_t len = frame[5];
    size_t expected_length = 7 + len;

    if (frame_length != expected_length) {
        return false;
    }

    uint8_t expected_crc = aog_crc_calculate(&frame[2], 4 + len);
    uint8_t received_crc = frame[6 + len];
    return expected_crc == received_crc;
}

/* ---- CRC Verification: TOLERANT (Discovery only) ----
 *
 * NAV-AOG-001-FINAL: Discovery frames from AgIO may have CRC issues.
 * Known AgIO inconsistencies:
 *   - CRC == 0x00 on some Discovery frames (uninitialized)
 *   - CRC off by ±1 (wire noise on UDP)
 *
 * Tolerant rules:
 *   1. If strict CRC passes → accept (normal case)
 *   2. If received CRC == 0x00 → accept (known AgIO quirk)
 *   3. If |expected - received| wraps to 1 → accept (±1 noise)
 *   4. Otherwise → reject
 *
 * WARNING: Must ONLY be used for Discovery PGNs (202, 253).
 *          Core PGNs MUST use strict verification.
 */

bool aog_frame_verify_crc_tolerant(const uint8_t* frame, size_t frame_length)
{
    /* Check basic frame integrity first */
    /* Minimum frame: preamble(2) + src(1) + pgn(2) + len(1) + crc(1) = 7 */
    if (frame_length < 7) {
        return false;
    }

    if (frame[0] != AOG_PREAMBLE_1 || frame[1] != AOG_PREAMBLE_2) {
        return false;
    }

    uint8_t len = frame[5];
    size_t expected_length = 7 + len;

    if (frame_length != expected_length) {
        return false;
    }

    uint8_t expected_crc = aog_crc_calculate(&frame[2], 4 + len);
    uint8_t received_crc = frame[6 + len];

    /* Rule 1: strict match — always accept */
    if (expected_crc == received_crc) {
        return true;
    }

    /* Rule 2: CRC == 0x00 — known AgIO quirk on Discovery frames */
    if (received_crc == 0x00) {
        return true;
    }

    /* Rule 3: ±1 wrap-around tolerance (wire noise) */
    uint8_t diff = (uint8_t)((expected_crc - received_crc) & 0xFF);
    if (diff == 1 || diff == 0xFF) {
        return true;
    }

    /* Rule 4: reject */
    return false;
}

/* ---- PGN Classification ---- */

bool aog_pgn_is_discovery(uint16_t pgn)
{
    return (pgn == AOG_PGN_SCAN_REQUEST)   /* 202 */
        || (pgn == AOG_PGN_SCAN_REPLY)     /* 203 */
        || (pgn == AOG_PGN_HELLO_REQUEST)  /* 253 */
        || (pgn == AOG_PGN_HELLO_RESPONSE); /* 254 */
}

/* ---- Streaming Parser ---- */

void aog_parser_init(aog_parser_t* parser)
{
    memset(parser, 0, sizeof(aog_parser_t));
    parser->state = AOG_PARSE_IDLE;
}

void aog_parser_init_ex(aog_parser_t* parser, bool discovery_tolerant)
{
    memset(parser, 0, sizeof(aog_parser_t));
    parser->state = AOG_PARSE_IDLE;
    parser->discovery_tolerant = discovery_tolerant;
}

bool aog_parser_feed(aog_parser_t* parser, uint8_t byte)
{
    switch (parser->state) {

    case AOG_PARSE_IDLE:
        if (byte == AOG_PREAMBLE_1) {
            parser->state = AOG_PARSE_PREAMBLE;
        }
        return false;

    case AOG_PARSE_PREAMBLE:
        if (byte == AOG_PREAMBLE_2) {
            parser->state = AOG_PARSE_SRC;
        } else {
            parser->state = AOG_PARSE_IDLE;
            if (byte == AOG_PREAMBLE_1) {
                parser->state = AOG_PARSE_PREAMBLE;
            }
        }
        return false;

    case AOG_PARSE_SRC:
        parser->src = byte;
        parser->state = AOG_PARSE_PGN;
        parser->data_count = 0;
        parser->pgn = 0;
        return false;

    case AOG_PARSE_PGN:
        if (parser->data_count == 0) {
            parser->pgn = (uint16_t)byte;
            parser->data_count = 1;
        } else {
            parser->pgn |= (uint16_t)((uint16_t)byte << 8);
            parser->data_count = 2;
            parser->state = AOG_PARSE_LENGTH;
        }
        return false;

    case AOG_PARSE_LENGTH:
        parser->length = byte;
        parser->data_count = 0;
        if (parser->length == 0) {
            parser->state = AOG_PARSE_CRC;
        } else {
            parser->state = AOG_PARSE_DATA;
        }
        return false;

    case AOG_PARSE_DATA:
        if (parser->data_count < AOG_MAX_DATA_SIZE) {
            parser->data[parser->data_count] = byte;
        }
        parser->data_count++;
        if (parser->data_count >= parser->length) {
            parser->state = AOG_PARSE_CRC;
        }
        return false;

    case AOG_PARSE_CRC: {
        /* CRC = sum(SRC + PGN_lo + PGN_hi + LEN + DATA[0..length-1]) mod 256 */
        uint32_t calc = (uint32_t)parser->src;
        calc += (uint32_t)(parser->pgn & 0xFF);
        calc += (uint32_t)((parser->pgn >> 8) & 0xFF);
        calc += (uint32_t)parser->length;
        for (uint8_t i = 0; i < parser->length && i < AOG_MAX_DATA_SIZE; i++) {
            calc += (uint32_t)parser->data[i];
        }

        uint8_t expected_crc = (uint8_t)(calc & 0xFF);

        if (parser->discovery_tolerant && aog_pgn_is_discovery(parser->pgn)) {
            /* NAV-AOG-001-FINAL: Tolerant CRC for Discovery PGNs */
            if (expected_crc == byte) {
                parser->crc_valid = true;
            } else if (byte == 0x00) {
                /* Known AgIO quirk: uninitialized CRC on Discovery frames */
                parser->crc_valid = true;
            } else {
                uint8_t diff = (uint8_t)((expected_crc - byte) & 0xFF);
                parser->crc_valid = (diff == 1 || diff == 0xFF);
            }
        } else {
            /* Core PGNs or non-tolerant mode: strict CRC */
            parser->crc_valid = (expected_crc == byte);
        }

        parser->frame_ready = true;
        parser->state = AOG_PARSE_IDLE;
        return true;
    }

    default:
        parser->state = AOG_PARSE_IDLE;
        return false;
    }
}
