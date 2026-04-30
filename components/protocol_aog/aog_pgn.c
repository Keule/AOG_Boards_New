#include "aog_pgn.h"
#include "aog_frame.h"
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- Helpers: little-endian read/write ---- */

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

static void write_float_le(uint8_t* buf, float val)
{
    memcpy(buf, &val, sizeof(float));
}

static float read_float_le(const uint8_t* buf)
{
    float val;
    memcpy(&val, buf, sizeof(float));
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

static void write_int16_le(uint8_t* buf, int16_t val)
{
    buf[0] = (uint8_t)(val & 0xFF);
    buf[1] = (uint8_t)((val >> 8) & 0xFF);
}

static int16_t read_int16_le(const uint8_t* buf)
{
    return (int16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
}

/* ---- Fix Quality Mapping ---- */

uint8_t aog_fix_quality_to_aog(uint8_t gnss_fix_quality)
{
    switch (gnss_fix_quality) {
    case 1:  return AOG_FIX_GPS;       /* SINGLE → GPS */
    case 2:  return AOG_FIX_DGPS;      /* DGPS */
    case 4:  return AOG_FIX_RTK_FIX;   /* RTK FIXED */
    case 5:  return AOG_FIX_RTK_FLOAT; /* RTK FLOAT */
    default: return AOG_FIX_NONE;      /* NONE / unknown */
    }
}

/* ---- PGN 214 Sentinel Values ---- */

void aog_pgn214_set_sentinels(aog_pgn214_t* data)
{
    if (data == NULL) return;
    data->longitude       = AOG_SENTINEL_DOUBLE;
    data->latitude        = AOG_SENTINEL_DOUBLE;
    data->heading_dual    = AOG_SENTINEL_FLOAT;
    data->heading_true    = AOG_SENTINEL_FLOAT;
    data->speed_kmh       = AOG_SENTINEL_FLOAT;
    data->roll            = AOG_SENTINEL_FLOAT;
    data->altitude        = AOG_SENTINEL_FLOAT;
    data->satellites      = AOG_SENTINEL_U16;
    data->fix_quality     = AOG_FIX_NONE;
    data->hdop_x100       = AOG_SENTINEL_U16;
    data->age_x100        = AOG_SENTINEL_U16;
    data->imu_heading_x10 = AOG_SENTINEL_U16;
    data->imu_roll_x10    = AOG_SENTINEL_I16;
    data->imu_pitch       = AOG_SENTINEL_I16;
    data->imu_yaw_rate    = AOG_SENTINEL_I16;
}

/* ---- PGN 214 Encode ---- */

uint8_t aog_pgn_encode_pgn214(uint8_t* buffer, const aog_pgn214_t* data)
{
    if (buffer == NULL || data == NULL) {
        return 0;
    }

    /* Byte 0-7: longitude (f64 LE) */
    write_double_le(&buffer[0], data->longitude);

    /* Byte 8-15: latitude (f64 LE) */
    write_double_le(&buffer[8], data->latitude);

    /* Byte 16-19: heading_dual (f32 LE, degrees) */
    write_float_le(&buffer[16], data->heading_dual);

    /* Byte 20-23: heading_true (f32 LE, degrees) */
    write_float_le(&buffer[20], data->heading_true);

    /* Byte 24-27: speed (f32 LE, km/h) */
    write_float_le(&buffer[24], data->speed_kmh);

    /* Byte 28-31: roll (f32 LE, degrees) */
    write_float_le(&buffer[28], data->roll);

    /* Byte 32-35: altitude (f32 LE, meters) */
    write_float_le(&buffer[32], data->altitude);

    /* Byte 36-37: satellites (u16 LE) */
    write_uint16_le(&buffer[36], data->satellites);

    /* Byte 38: fix_quality (u8) */
    buffer[38] = data->fix_quality;

    /* Byte 39-40: hdop × 100 (u16 LE) */
    write_uint16_le(&buffer[39], data->hdop_x100);

    /* Byte 41-42: age × 100 (u16 LE) */
    write_uint16_le(&buffer[41], data->age_x100);

    /* Byte 43-44: imu_heading × 10 (u16 LE) */
    write_uint16_le(&buffer[43], data->imu_heading_x10);

    /* Byte 45-46: imu_roll × 10 (i16 LE) */
    write_int16_le(&buffer[45], data->imu_roll_x10);

    /* Byte 47-48: imu_pitch (i16 LE) */
    write_int16_le(&buffer[47], data->imu_pitch);

    /* Byte 49-50: imu_yaw_rate (i16 LE) */
    write_int16_le(&buffer[49], data->imu_yaw_rate);

    return AOG_PGN214_DATA_SIZE;
}

/* ---- PGN 214 Decode ---- */

bool aog_pgn_decode_pgn214(const uint8_t* data, uint8_t data_length, aog_pgn214_t* out)
{
    if (data == NULL || out == NULL || data_length < AOG_PGN214_DATA_SIZE) {
        return false;
    }

    out->longitude       = read_double_le(&data[0]);
    out->latitude        = read_double_le(&data[8]);
    out->heading_dual    = read_float_le(&data[16]);
    out->heading_true    = read_float_le(&data[20]);
    out->speed_kmh       = read_float_le(&data[24]);
    out->roll            = read_float_le(&data[28]);
    out->altitude        = read_float_le(&data[32]);
    out->satellites      = read_uint16_le(&data[36]);
    out->fix_quality     = data[38];
    out->hdop_x100       = read_uint16_le(&data[39]);
    out->age_x100        = read_uint16_le(&data[41]);
    out->imu_heading_x10 = read_uint16_le(&data[43]);
    out->imu_roll_x10    = read_int16_le(&data[45]);
    out->imu_pitch       = read_int16_le(&data[47]);
    out->imu_yaw_rate    = read_int16_le(&data[49]);

    return true;
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

/* ---- PGN 203: Scan Reply ---- */

uint8_t aog_pgn_encode_scan_reply(uint8_t* buffer, const aog_scan_reply_t* reply)
{
    if (buffer == NULL || reply == NULL) {
        return 0;
    }

    uint8_t count = reply->pgn_count;
    if (count > AOG_SCAN_REPLY_MAX_PGNS) {
        count = AOG_SCAN_REPLY_MAX_PGNS;
    }

    buffer[0] = reply->src;
    buffer[1] = reply->module_type;
    write_uint16_le(&buffer[2], (uint16_t)count);

    for (uint8_t i = 0; i < count; i++) {
        write_uint16_le(&buffer[4 + i * 2], reply->pgns[i]);
    }

    return 4 + count * 2;
}

/* ---- PGN 200: GPS Position Output (legacy) ---- */

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

/* ---- PGN 201: Heading Output (legacy) ---- */

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
