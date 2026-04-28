#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NMEA_MAX_SENTENCE_LEN 128U

typedef enum {
    NMEA_SENTENCE_UNKNOWN = 0,
    NMEA_SENTENCE_GGA,
    NMEA_SENTENCE_RMC,
    NMEA_SENTENCE_GST,
    NMEA_SENTENCE_GSV,
    NMEA_SENTENCE_GSA
} nmea_sentence_type_t;

typedef struct {
    nmea_sentence_type_t type;
    char sentence[NMEA_MAX_SENTENCE_LEN];
    bool checksum_valid;
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
