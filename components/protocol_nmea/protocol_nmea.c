#include "protocol_nmea.h"

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
    uint8_t rx = 0;
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

        rx = (uint8_t)((hi << 4U) | lo);
    }

    return (checksum == rx);
}

static nmea_sentence_type_t nmea_detect_type(const char* sentence)
{
    if (sentence == NULL || sentence[0] != '$') {
        return NMEA_SENTENCE_UNKNOWN;
    }

    if (strstr(sentence, "GGA,") != NULL) {
        return NMEA_SENTENCE_GGA;
    }

    if (strstr(sentence, "RMC,") != NULL) {
        return NMEA_SENTENCE_RMC;
    }

    if (strstr(sentence, "GST,") != NULL) {
        return NMEA_SENTENCE_GST;
    }

    if (strstr(sentence, "GSV,") != NULL) {
        return NMEA_SENTENCE_GSV;
    }

    if (strstr(sentence, "GSA,") != NULL) {
        return NMEA_SENTENCE_GSA;
    }

    return NMEA_SENTENCE_UNKNOWN;
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
    memcpy(out_message->sentence, parser->buffer, parser->length + 1U);
    out_message->checksum_valid = nmea_verify_checksum(parser->buffer);
    out_message->type = nmea_detect_type(parser->buffer);

    parser->length = 0;
    return true;
}
