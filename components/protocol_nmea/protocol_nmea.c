#include "protocol_nmea.h"

#include <stdlib.h>
#include <string.h>

static uint8_t parse_hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return (uint8_t)(c - '0');
    }
    if (c >= 'A' && c <= 'F') {
        return (uint8_t)(10 + (c - 'A'));
    }
    if (c >= 'a' && c <= 'f') {
        return (uint8_t)(10 + (c - 'a'));
    }
    return 0xFFU;
}

static bool nmea_verify_checksum(const char* sentence)
{
    uint8_t checksum = 0;
    size_t i = 1;
    size_t star_pos = 0;

    if (sentence == NULL || sentence[0] != '$') {
        return false;
    }

    while (sentence[i] != '\0' && sentence[i] != '*' && i < NMEA_MAX_SENTENCE_LEN) {
        checksum ^= (uint8_t)sentence[i];
        i++;
    }

    if (sentence[i] != '*') {
        return false;
    }

    star_pos = i;

    if (sentence[star_pos + 1] == '\0' || sentence[star_pos + 2] == '\0') {
        return false;
    }

    {
        uint8_t hi = parse_hex_nibble(sentence[star_pos + 1]);
        uint8_t lo = parse_hex_nibble(sentence[star_pos + 2]);
        if (hi == 0xFFU || lo == 0xFFU) {
            return false;
        }
        return checksum == (uint8_t)((hi << 4U) | lo);
    }
}

static nmea_sentence_type_t detect_type(const char* id)
{
    if (id == NULL || strlen(id) < 5U) {
        return NMEA_SENTENCE_UNKNOWN;
    }

    if (strcmp(&id[2], "GGA") == 0) return NMEA_SENTENCE_GGA;
    if (strcmp(&id[2], "RMC") == 0) return NMEA_SENTENCE_RMC;
    if (strcmp(&id[2], "GST") == 0) return NMEA_SENTENCE_GST;
    if (strcmp(&id[2], "GSV") == 0) return NMEA_SENTENCE_GSV;
    if (strcmp(&id[2], "GSA") == 0) return NMEA_SENTENCE_GSA;

    return NMEA_SENTENCE_UNKNOWN;
}

static double parse_latlon_deg(const char* value, const char* hemi)
{
    double raw = 0.0;
    int deg = 0;
    double min = 0.0;
    double out = 0.0;

    if (value == NULL || hemi == NULL || value[0] == '\0' || hemi[0] == '\0') {
        return 0.0;
    }

    raw = strtod(value, NULL);
    deg = (int)(raw / 100.0);
    min = raw - ((double)deg * 100.0);
    out = (double)deg + (min / 60.0);

    if (hemi[0] == 'S' || hemi[0] == 'W') {
        out = -out;
    }

    return out;
}

static size_t split_csv(char* text, char* fields[], size_t max_fields)
{
    size_t count = 0;
    char* p = text;

    if (text == NULL || fields == NULL || max_fields == 0) {
        return 0;
    }

    fields[count++] = p;

    while (*p != '\0' && count < max_fields) {
        if (*p == ',') {
            *p = '\0';
            fields[count++] = p + 1;
        }
        p++;
    }

    return count;
}

static nmea_parse_status_t parse_body(const char* sentence, nmea_message_t* out)
{
    char work[NMEA_MAX_SENTENCE_LEN];
    char* fields[32];
    size_t n = 0;
    char* star = NULL;

    memset(work, 0, sizeof(work));
    strncpy(work, sentence + 1, sizeof(work) - 1U);

    star = strchr(work, '*');
    if (star != NULL) {
        *star = '\0';
    }

    n = split_csv(work, fields, 32);
    if (n == 0) {
        return NMEA_PARSE_FORMAT_ERROR;
    }

    out->type = detect_type(fields[0]);

    switch (out->type) {
        case NMEA_SENTENCE_GGA:
            if (n < 10) return NMEA_PARSE_FORMAT_ERROR;
            out->data.gga.utc_time = strtod(fields[1], NULL);
            out->data.gga.latitude_deg = parse_latlon_deg(fields[2], fields[3]);
            out->data.gga.longitude_deg = parse_latlon_deg(fields[4], fields[5]);
            out->data.gga.fix_quality = (uint8_t)strtoul(fields[6], NULL, 10);
            out->data.gga.satellites = (uint8_t)strtoul(fields[7], NULL, 10);
            out->data.gga.hdop = strtod(fields[8], NULL);
            out->data.gga.altitude_m = strtod(fields[9], NULL);
            return NMEA_PARSE_OK;
        case NMEA_SENTENCE_RMC:
            if (n < 10) return NMEA_PARSE_FORMAT_ERROR;
            out->data.rmc.utc_time = strtod(fields[1], NULL);
            out->data.rmc.status = fields[2][0];
            out->data.rmc.latitude_deg = parse_latlon_deg(fields[3], fields[4]);
            out->data.rmc.longitude_deg = parse_latlon_deg(fields[5], fields[6]);
            out->data.rmc.speed_over_ground_kn = strtod(fields[7], NULL);
            out->data.rmc.course_over_ground_deg = strtod(fields[8], NULL);
            out->data.rmc.date_ddmmyy = (uint32_t)strtoul(fields[9], NULL, 10);
            return NMEA_PARSE_OK;
        case NMEA_SENTENCE_GST:
            if (n < 9) return NMEA_PARSE_FORMAT_ERROR;
            out->data.gst.rms = strtod(fields[2], NULL);
            out->data.gst.latitude_stddev = strtod(fields[6], NULL);
            out->data.gst.longitude_stddev = strtod(fields[7], NULL);
            out->data.gst.altitude_stddev = strtod(fields[8], NULL);
            return NMEA_PARSE_OK;
        case NMEA_SENTENCE_GSV:
            if (n < 4) return NMEA_PARSE_FORMAT_ERROR;
            out->data.gsv.total_messages = (uint8_t)strtoul(fields[1], NULL, 10);
            out->data.gsv.message_index = (uint8_t)strtoul(fields[2], NULL, 10);
            out->data.gsv.total_satellites_in_view = (uint8_t)strtoul(fields[3], NULL, 10);
            return NMEA_PARSE_OK;
        case NMEA_SENTENCE_GSA:
            if (n < 18) return NMEA_PARSE_FORMAT_ERROR;
            out->data.gsa.fix_type = (uint8_t)strtoul(fields[2], NULL, 10);
            out->data.gsa.pdop = strtod(fields[15], NULL);
            out->data.gsa.hdop = strtod(fields[16], NULL);
            out->data.gsa.vdop = strtod(fields[17], NULL);
            return NMEA_PARSE_OK;
        default:
            return NMEA_PARSE_UNSUPPORTED;
    }
}

void nmea_parser_init(nmea_parser_t* parser)
{
    if (parser == NULL) {
        return;
    }
    memset(parser->buffer, 0, sizeof(parser->buffer));
    parser->length = 0;
}

bool nmea_parser_feed(nmea_parser_t* parser, char byte, nmea_message_t* out_message)
{
    if (parser == NULL || out_message == NULL) {
        return false;
    }

    if (byte == '$') {
        parser->length = 0;
        parser->buffer[parser->length++] = byte;
        return false;
    }

    if (parser->length == 0) {
        return false;
    }

    if (parser->length >= (NMEA_MAX_SENTENCE_LEN - 1U)) {
        parser->length = 0;
        return false;
    }

    parser->buffer[parser->length++] = byte;

    if (byte != '\n') {
        return false;
    }

    parser->buffer[parser->length] = '\0';
    memset(out_message, 0, sizeof(*out_message));

    out_message->checksum_valid = nmea_verify_checksum(parser->buffer);
    if (!out_message->checksum_valid) {
        out_message->status = NMEA_PARSE_CHECKSUM_ERROR;
        out_message->type = NMEA_SENTENCE_UNKNOWN;
    } else {
        out_message->status = parse_body(parser->buffer, out_message);
    }

    parser->length = 0;
    return true;
}
