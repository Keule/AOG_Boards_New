#include "aog_frame.h"
#include <string.h>

uint8_t aog_crc_calculate(const uint8_t* data, size_t length)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
    }
    return crc;
}

size_t aog_frame_encode(uint8_t* buffer, uint16_t pgn, const uint8_t* data, uint8_t data_length)
{
    if (data_length > AOG_MAX_DATA_SIZE) {
        return 0;
    }

    uint8_t length = 2 + data_length;

    buffer[0] = AOG_PREAMBLE_1;
    buffer[1] = AOG_PREAMBLE_2;
    buffer[2] = length;
    buffer[3] = (uint8_t)(pgn & 0xFF);
    buffer[4] = (uint8_t)((pgn >> 8) & 0xFF);

    if (data_length > 0 && data != NULL) {
        memcpy(&buffer[5], data, data_length);
    }

    /* CRC covers bytes from index 2 (length) through last data byte */
    uint8_t crc = aog_crc_calculate(&buffer[2], 3 + data_length);
    buffer[5 + data_length] = crc;

    return 6 + data_length;
}

bool aog_frame_verify_crc(const uint8_t* frame, size_t frame_length)
{
    if (frame_length < 6) {
        return false;
    }

    uint8_t length = frame[2];
    size_t expected_length = (size_t)length + 4; /* 2 preamble + 1 length + length + 1 crc */

    if (frame_length != expected_length) {
        return false;
    }

    if (frame[0] != AOG_PREAMBLE_1 || frame[1] != AOG_PREAMBLE_2) {
        return false;
    }

    uint8_t expected_crc = aog_crc_calculate(&frame[2], (size_t)length + 1);
    uint8_t received_crc = frame[2 + length + 1];

    return expected_crc == received_crc;
}

void aog_parser_init(aog_parser_t* parser)
{
    memset(parser, 0, sizeof(aog_parser_t));
    parser->state = AOG_PARSE_IDLE;
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
            parser->state = AOG_PARSE_LENGTH;
        } else {
            /* Not a valid preamble, check if this is start of new preamble */
            parser->state = AOG_PARSE_IDLE;
            if (byte == AOG_PREAMBLE_1) {
                parser->state = AOG_PARSE_PREAMBLE;
            }
        }
        return false;

    case AOG_PARSE_LENGTH:
        parser->length = byte;
        if (parser->length < 2) {
            /* Length must at least cover PGN (2 bytes) */
            parser->state = AOG_PARSE_IDLE;
            return false;
        }
        parser->data_count = 0;
        parser->pgn = 0;
        parser->state = AOG_PARSE_PGN;
        return false;

    case AOG_PARSE_PGN:
        if (parser->data_count == 0) {
            parser->pgn = (uint16_t)byte;
            parser->data_count = 1;
        } else {
            parser->pgn |= (uint16_t)((uint16_t)byte << 8);
            parser->data_count = 2;
            /* Check if there is data to read */
            uint8_t data_length = parser->length - 2;
            if (data_length == 0) {
                parser->state = AOG_PARSE_CRC;
            } else {
                parser->data_count = 0;
                parser->state = AOG_PARSE_DATA;
            }
        }
        return false;

    case AOG_PARSE_DATA: {
        uint8_t data_length = parser->length - 2;
        if (parser->data_count < data_length) {
            if (parser->data_count < AOG_MAX_DATA_SIZE) {
                parser->data[parser->data_count] = byte;
            }
            parser->data_count++;
            if (parser->data_count >= data_length) {
                parser->state = AOG_PARSE_CRC;
            }
        }
        return false;
    }

    case AOG_PARSE_CRC: {
        /* Calculate expected CRC: XOR of length, PGN_lo, PGN_hi, data */
        uint8_t calc_crc = parser->length;
        calc_crc ^= (uint8_t)(parser->pgn & 0xFF);
        calc_crc ^= (uint8_t)((parser->pgn >> 8) & 0xFF);
        uint8_t data_length = parser->length - 2;
        for (uint8_t i = 0; i < data_length && i < AOG_MAX_DATA_SIZE; i++) {
            calc_crc ^= parser->data[i];
        }

        parser->crc_valid = (calc_crc == byte);
        parser->frame_ready = true;
        parser->state = AOG_PARSE_IDLE;
        return true;
    }

    default:
        parser->state = AOG_PARSE_IDLE;
        return false;
    }
}
