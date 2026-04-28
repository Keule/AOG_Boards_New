#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NMEA_MAX_SENTENCE_LEN 128U

typedef enum {
    NMEA_PARSE_NONE = 0,
    NMEA_PARSE_OK,
    NMEA_PARSE_CHECKSUM_ERROR,
    NMEA_PARSE_UNSUPPORTED,
    NMEA_PARSE_FORMAT_ERROR
} nmea_parse_status_t;

typedef enum {
    NMEA_SENTENCE_UNKNOWN = 0,
    NMEA_SENTENCE_GGA,
    NMEA_SENTENCE_RMC,
    NMEA_SENTENCE_GST,
    NMEA_SENTENCE_GSV,
    NMEA_SENTENCE_GSA
} nmea_sentence_type_t;

typedef struct {
    double utc_time;
    double latitude_deg;
    double longitude_deg;
    uint8_t fix_quality;
    uint8_t satellites;
    double hdop;
    double altitude_m;
} nmea_gga_data_t;

typedef struct {
    double utc_time;
    uint32_t date_ddmmyy;
    char status;
    double latitude_deg;
    double longitude_deg;
    double speed_over_ground_kn;
    double course_over_ground_deg;
} nmea_rmc_data_t;

typedef struct {
    double rms;
    double latitude_stddev;
    double longitude_stddev;
    double altitude_stddev;
} nmea_gst_data_t;

typedef struct {
    uint8_t total_messages;
    uint8_t message_index;
    uint8_t total_satellites_in_view;
} nmea_gsv_data_t;

typedef struct {
    uint8_t fix_type;
    double pdop;
    double hdop;
    double vdop;
} nmea_gsa_data_t;

typedef struct {
    nmea_sentence_type_t type;
    nmea_parse_status_t status;
    bool checksum_valid;
    union {
        nmea_gga_data_t gga;
        nmea_rmc_data_t rmc;
        nmea_gst_data_t gst;
        nmea_gsv_data_t gsv;
        nmea_gsa_data_t gsa;
    } data;
} nmea_message_t;

typedef struct {
    char buffer[NMEA_MAX_SENTENCE_LEN];
    size_t length;
} nmea_parser_t;

void nmea_parser_init(nmea_parser_t* parser);
bool nmea_parser_feed(nmea_parser_t* parser, char byte, nmea_message_t* out_message);

#ifdef __cplusplus
}
#endif
