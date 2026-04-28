#include "protocol_aog.h"

#include <string.h>

static void write_u16_le(uint8_t* out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xFFU);
    out[1] = (uint8_t)((value >> 8U) & 0xFFU);
}

static uint16_t read_u16_le(const uint8_t* in)
{
    return (uint16_t)in[0] | ((uint16_t)in[1] << 8U);
}

uint16_t aog_crc16(const uint8_t* data, size_t length)
{
    uint16_t crc = 0xFFFFU;
    size_t i = 0;

    if (data == NULL) {
        return 0;
    }

    for (i = 0; i < length; i++) {
        uint8_t bit = 0;

        crc ^= (uint16_t)data[i] << 8U;

        for (bit = 0; bit < 8U; bit++) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((crc << 1U) ^ 0x1021U);
            } else {
                crc <<= 1U;
            }
        }
    }

    return crc;
}

bool aog_encode_frame(const aog_frame_t* frame, uint8_t* out, size_t out_capacity, size_t* out_size)
{
    size_t total_size = 0;
    uint16_t crc = 0;

    if (frame == NULL || out == NULL || out_size == NULL) {
        return false;
    }

    if (frame->payload_len > AOG_FRAME_MAX_PAYLOAD) {
        return false;
    }

    total_size = 1U + 2U + 2U + frame->payload_len + 2U;

    if (out_capacity < total_size) {
        return false;
    }

    out[0] = AOG_FRAME_START_BYTE;
    write_u16_le(&out[1], frame->payload_len);
    write_u16_le(&out[3], frame->pgn);
    memcpy(&out[5], frame->payload, frame->payload_len);

    crc = aog_crc16(&out[1], 2U + 2U + frame->payload_len);
    write_u16_le(&out[5U + frame->payload_len], crc);

    *out_size = total_size;
    return true;
}

bool aog_decode_frame(const uint8_t* data, size_t data_len, aog_frame_t* out_frame)
{
    uint16_t payload_len = 0;
    uint16_t expected_crc = 0;
    uint16_t received_crc = 0;

    if (data == NULL || out_frame == NULL) {
        return false;
    }

    if (data_len < (1U + 2U + 2U + 2U)) {
        return false;
    }

    if (data[0] != AOG_FRAME_START_BYTE) {
        return false;
    }

    payload_len = read_u16_le(&data[1]);

    if (payload_len > AOG_FRAME_MAX_PAYLOAD) {
        return false;
    }

    if (data_len != (size_t)(1U + 2U + 2U + payload_len + 2U)) {
        return false;
    }

    out_frame->pgn = read_u16_le(&data[3]);
    out_frame->payload_len = payload_len;
    memcpy(out_frame->payload, &data[5], payload_len);

    expected_crc = aog_crc16(&data[1], (size_t)(2U + 2U + payload_len));
    received_crc = read_u16_le(&data[5U + payload_len]);

    return (expected_crc == received_crc);
}

bool aog_build_hello_response(const aog_version_t* version, aog_frame_t* out_frame)
{
    if (version == NULL || out_frame == NULL) {
        return false;
    }

    out_frame->pgn = (uint16_t)AOG_PGN_HELLO_RESPONSE;
    out_frame->payload_len = 3;
    out_frame->payload[0] = version->major;
    out_frame->payload[1] = version->minor;
    out_frame->payload[2] = version->patch;

    return true;
}

bool aog_build_position_out(const aog_position_t* position, aog_frame_t* out_frame)
{
    if (position == NULL || out_frame == NULL) {
        return false;
    }

    out_frame->pgn = (uint16_t)AOG_PGN_POSITION_OUT;
    out_frame->payload_len = 12;
    memcpy(&out_frame->payload[0], &position->latitude_e7, 4);
    memcpy(&out_frame->payload[4], &position->longitude_e7, 4);
    memcpy(&out_frame->payload[8], &position->altitude_mm, 4);

    return true;
}

bool aog_build_heading_out(const aog_heading_t* heading, aog_frame_t* out_frame)
{
    if (heading == NULL || out_frame == NULL) {
        return false;
    }

    out_frame->pgn = (uint16_t)AOG_PGN_HEADING_OUT;
    out_frame->payload_len = 4;
    memcpy(&out_frame->payload[0], &heading->heading_mdeg, 4);

    return true;
}
