#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AOG_FRAME_MAX_PAYLOAD 128U
#define AOG_FRAME_START_BYTE 0x7EU

/* Internal placeholder IDs.
 * TODO: replace with verified AgOpenGPS PGNs. */
typedef enum {
    AOG_INTERNAL_ID_HELLO_REQUEST = 0x7000,
    AOG_INTERNAL_ID_HELLO_RESPONSE = 0x7001,
    AOG_INTERNAL_ID_POSITION_OUT = 0x7002,
    AOG_INTERNAL_ID_HEADING_OUT = 0x7003
} aog_internal_id_t;

typedef struct {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
} aog_version_t;

typedef struct {
    int32_t latitude_e7;
    int32_t longitude_e7;
    int32_t altitude_mm;
} aog_position_t;

typedef struct {
    int32_t heading_mdeg;
} aog_heading_t;

typedef struct {
    uint16_t pgn;
    uint8_t payload[AOG_FRAME_MAX_PAYLOAD];
    uint16_t payload_len;
} aog_frame_t;

uint16_t aog_crc16(const uint8_t* data, size_t length);
bool aog_encode_frame(const aog_frame_t* frame, uint8_t* out, size_t out_capacity, size_t* out_size);
bool aog_decode_frame(const uint8_t* data, size_t data_len, aog_frame_t* out_frame);
bool aog_is_hello_request(const aog_frame_t* frame);
bool aog_build_discovery_response(const aog_version_t* version, aog_frame_t* out_frame);
bool aog_build_position_out(const aog_position_t* position, aog_frame_t* out_frame);
bool aog_build_heading_out(const aog_heading_t* heading, aog_frame_t* out_frame);

#ifdef __cplusplus
}
#endif
