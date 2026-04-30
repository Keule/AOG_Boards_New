#include "aog_pgn.h"
#include <string.h>

/* ---- Helper: read/write little-endian primitives ---- */

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
    write_uint16_le(buf, (uint16_t)val);
}

static int16_t read_int16_le(const uint8_t* buf)
{
    return (int16_t)read_uint16_le(buf);
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

/* ========================================================================
 * PGN 214: Combined GPS + Heading Output (NAV-AOG-001)
 *
 * Wire layout (51 bytes, all little-endian):
 *   [ 0.. 7] : longitude       float64
 *   [ 8..15] : latitude        float64
 *   [16..19] : heading_dual    float32  (radians)
 *   [20..23] : heading_true    float32  (radians)
 *   [24..27] : speed           float32  (m/s)
 *   [28..31] : roll            float32  (radians)
 *   [32..35] : altitude        float32  (meters)
 *   [36..37] : satellites      uint16
 *   [38]    : fix_quality     uint8
 *   [39..40] : hdop            uint16   (×100)
 *   [41..42] : age             uint16   (×100)
 *   [43..44] : imu_heading     uint16   (×10)
 *   [45..46] : imu_roll        int16    (×10)
 *   [47..48] : imu_pitch       int16
 *   [49..50] : imu_yaw_rate    int16
 * ======================================================================== */

uint8_t aog_pgn_encode_214(uint8_t* buffer, const aog_pgn214_t* data)
{
    if (buffer == NULL || data == NULL) {
        return 0;
    }

    /* Position */
    write_double_le(&buffer[0], data->longitude);
    write_double_le(&buffer[8], data->latitude);

    /* Heading */
    write_float_le(&buffer[16], data->heading_dual);
    write_float_le(&buffer[20], data->heading_true);

    /* Motion */
    write_float_le(&buffer[24], data->speed);
    write_float_le(&buffer[28], data->roll);

    /* Altitude */
    write_float_le(&buffer[32], data->altitude);

    /* Fix info */
    write_uint16_le(&buffer[36], data->satellites);
    buffer[38] = data->fix_quality;
    write_uint16_le(&buffer[39], data->hdop_x100);

    /* Correction age */
    write_uint16_le(&buffer[41], data->age_x100);

    /* IMU */
    write_uint16_le(&buffer[43], data->imu_heading_x10);
    write_int16_le(&buffer[45], data->imu_roll_x10);
    write_int16_le(&buffer[47], data->imu_pitch);
    write_int16_le(&buffer[49], data->imu_yaw_rate);

    return AOG_PGN214_DATA_SIZE;
}

bool aog_pgn_decode_214(const uint8_t* data, uint8_t data_length, aog_pgn214_t* out)
{
    if (data == NULL || out == NULL || data_length < AOG_PGN214_DATA_SIZE) {
        return false;
    }

    /* Position */
    out->longitude   = read_double_le(&data[0]);
    out->latitude    = read_double_le(&data[8]);

    /* Heading */
    out->heading_dual = read_float_le(&data[16]);
    out->heading_true = read_float_le(&data[20]);

    /* Motion */
    out->speed = read_float_le(&data[24]);
    out->roll  = read_float_le(&data[28]);

    /* Altitude */
    out->altitude = read_float_le(&data[32]);

    /* Fix info */
    out->satellites = read_uint16_le(&data[36]);
    out->fix_quality = data[38];
    out->hdop_x100  = read_uint16_le(&data[39]);

    /* Correction age */
    out->age_x100 = read_uint16_le(&data[41]);

    /* IMU */
    out->imu_heading_x10 = read_uint16_le(&data[43]);
    out->imu_roll_x10    = read_int16_le(&data[45]);
    out->imu_pitch       = read_int16_le(&data[47]);
    out->imu_yaw_rate    = read_int16_le(&data[49]);

    return true;
}
