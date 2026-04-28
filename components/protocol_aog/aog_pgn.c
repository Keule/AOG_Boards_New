#include "aog_pgn.h"
#include <string.h>

/* ---- Helper: read/write little-endian double ---- */

static void write_double_le(uint8_t* buf, double val)
{
    memcpy(buf, &val, sizeof(double));
}

static double read_double_le(const uint8_t* buf)
{
    double val;
    memcpy(&val, buf, sizeof(double));
    return val;
}

static void write_uint16_le(uint8_t* buf, uint16_t val)
{
    buf[0] = (uint8_t)(val & 0xFF);
    buf[1] = (uint8_t)((val >> 8) & 0xFF);
}

static uint16_t read_uint16_le(const uint8_t* buf)
{
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

/* ---- PGN 254: Hello/Discovery Response ---- */

uint8_t aog_pgn_encode_hello(uint8_t* buffer, const aog_hello_t* hello)
{
    if (buffer == NULL || hello == NULL) {
        return 0;
    }

    memcpy(&buffer[0], hello->ip, 4);
    write_uint16_le(&buffer[4], hello->port);
    buffer[6] = hello->subnet_index;

    return AOG_HELLO_DATA_SIZE;
}

bool aog_pgn_decode_hello(const uint8_t* data, uint8_t data_length, aog_hello_t* hello)
{
    if (data == NULL || hello == NULL || data_length < AOG_HELLO_DATA_SIZE) {
        return false;
    }

    memcpy(hello->ip, data, 4);
    hello->port = read_uint16_le(&data[4]);
    hello->subnet_index = data[6];

    return true;
}

/* ---- PGN 200: GPS Position Output ---- */

uint8_t aog_pgn_encode_position(uint8_t* buffer, const aog_position_t* pos)
{
    if (buffer == NULL || pos == NULL) {
        return 0;
    }

    buffer[0] = pos->fix;
    buffer[1] = pos->num_sats;
    write_double_le(&buffer[2], pos->latitude);
    write_double_le(&buffer[10], pos->longitude);

    return AOG_POSITION_DATA_SIZE;
}

bool aog_pgn_decode_position(const uint8_t* data, uint8_t data_length, aog_position_t* pos)
{
    if (data == NULL || pos == NULL || data_length < AOG_POSITION_DATA_SIZE) {
        return false;
    }

    pos->fix = data[0];
    pos->num_sats = data[1];
    pos->latitude = read_double_le(&data[2]);
    pos->longitude = read_double_le(&data[10]);

    return true;
}

/* ---- PGN 201: Heading Output ---- */

uint8_t aog_pgn_encode_heading(uint8_t* buffer, const aog_heading_t* hdg)
{
    if (buffer == NULL || hdg == NULL) {
        return 0;
    }

    write_double_le(&buffer[0], hdg->heading);
    write_double_le(&buffer[8], hdg->roll);

    return AOG_HEADING_DATA_SIZE;
}

bool aog_pgn_decode_heading(const uint8_t* data, uint8_t data_length, aog_heading_t* hdg)
{
    if (data == NULL || hdg == NULL || data_length < AOG_HEADING_DATA_SIZE) {
        return false;
    }

    hdg->heading = read_double_le(&data[0]);
    hdg->roll = read_double_le(&data[8]);

    return true;
}
