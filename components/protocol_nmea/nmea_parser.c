#include "nmea_parser.h"
#include <string.h>
#include <stdlib.h>

/* ---- Field extraction helpers ---- */

/* Get pointer to field data and its length.
 * sentence is the content between $ and * (no delimiters).
 * Returns pointer into sentence buffer, sets out_length.
 * Returns NULL if field_index is out of range. */
static const char* nmea_get_field(const char* sentence, uint8_t field_index, uint8_t* out_length)
{
    uint8_t idx = 0;
    const char* p = sentence;

    while (*p != '\0') {
        if (idx == field_index) {
            const char* start = p;
            uint8_t len = 0;
            while (*p != ',' && *p != '\0') {
                len++;
                p++;
            }
            *out_length = len;
            return start;
        }
        /* Skip to next field */
        while (*p != ',' && *p != '\0') {
            p++;
        }
        if (*p == ',') {
            p++;
        }
        idx++;
    }

    *out_length = 0;
    return NULL;
}

/* Parse a double from a field string.
 * Returns 0.0 for empty or invalid fields. */
static double nmea_parse_double(const char* str, uint8_t len)
{
    if (len == 0 || str == NULL) {
        return 0.0;
    }

    /* Create null-terminated copy for atof */
    char tmp[24];
    uint8_t copy_len = len;
    if (copy_len >= sizeof(tmp)) {
        copy_len = sizeof(tmp) - 1;
    }
    memcpy(tmp, str, copy_len);
    tmp[copy_len] = '\0';
    return atof(tmp);
}

/* Parse an integer from a field string.
 * Returns 0 for empty or invalid fields. */
static int nmea_parse_int(const char* str, uint8_t len)
{
    if (len == 0 || str == NULL) {
        return 0;
    }

    char tmp[12];
    uint8_t copy_len = len;
    if (copy_len >= sizeof(tmp)) {
        copy_len = sizeof(tmp) - 1;
    }
    memcpy(tmp, str, copy_len);
    tmp[copy_len] = '\0';
    return atoi(tmp);
}

/* Parse a single character from a field.
 * Returns '\0' for empty field. */
static char nmea_parse_char(const char* str, uint8_t len)
{
    if (len == 0 || str == NULL) {
        return '\0';
    }
    return str[0];
}

/* Convert NMEA lat/lon format (DDmm.mmmmm) to decimal degrees.
 * hemisphere: 'N'/'S' for latitude, 'E'/'W' for longitude.
 * Returns decimal degrees (N/E positive, S/W negative). */
static double nmea_parse_latlon(const char* str, uint8_t len, char hemisphere)
{
    double raw = nmea_parse_double(str, len);
    if (raw == 0.0) {
        return 0.0;
    }

    /* Extract degrees (first 2 digits for lat, 3 for lon) */
    int deg_len = 2; /* latitude */
    if (hemisphere == 'E' || hemisphere == 'W') {
        deg_len = 3; /* longitude */
    }

    char tmp[24];
    if (len >= sizeof(tmp)) {
        return 0.0;
    }
    memcpy(tmp, str, len);
    tmp[len] = '\0';

    /* Parse degrees */
    char deg_str[4] = {0};
    memcpy(deg_str, tmp, deg_len);
    int degrees = atoi(deg_str);

    /* Parse minutes */
    double minutes = atof(&tmp[deg_len]);

    double decimal = (double)degrees + minutes / 60.0;

    if (hemisphere == 'S' || hemisphere == 'W') {
        decimal = -decimal;
    }

    return decimal;
}

/* Parse NMEA date (ddmmyy) to day, month, year */
static void nmea_parse_date(const char* str, uint8_t len,
                            uint8_t* day, uint8_t* month, uint16_t* year)
{
    if (len < 6 || str == NULL) {
        *day = 0;
        *month = 0;
        *year = 0;
        return;
    }

    int d = 0, m = 0, y = 0;
    if (len >= 2) d = nmea_parse_int(str, 2);
    if (len >= 4) m = nmea_parse_int(&str[2], 2);
    if (len >= 6) y = nmea_parse_int(&str[4], 2);

    *day = (uint8_t)d;
    *month = (uint8_t)m;
    /* Convert 2-digit year to full year (assume 2000-2099) */
    *year = (uint16_t)(2000 + y);
}

/* Hex digit to value */
static int nmea_hex_val(uint8_t c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* ---- Sentence type identification ---- */

static nmea_sentence_type_t nmea_identify_sentence(const char* sentence)
{
    /* sentence starts with talker ID + sentence type, e.g. "GNGGA" */
    if (sentence[0] == 'G') {
        /* Skip talker ID (2 chars for standard, but could be variable) */
        /* Check sentence type starting at index 2 */
        if (sentence[2] == 'G' && sentence[3] == 'G' && sentence[4] == 'A') {
            return NMEA_SENTENCE_GGA;
        }
        if (sentence[2] == 'R' && sentence[3] == 'M' && sentence[4] == 'C') {
            return NMEA_SENTENCE_RMC;
        }
        if (sentence[2] == 'G' && sentence[3] == 'S' && sentence[4] == 'T') {
            return NMEA_SENTENCE_GST;
        }
        if (sentence[2] == 'G' && sentence[3] == 'S' && sentence[4] == 'V') {
            return NMEA_SENTENCE_GSV;
        }
        if (sentence[2] == 'G' && sentence[3] == 'S' && sentence[4] == 'A') {
            return NMEA_SENTENCE_GSA;
        }
    }
    return NMEA_SENTENCE_NONE;
}

/* ---- Sentence-specific parsers ---- */

static void nmea_parse_gga(nmea_parser_t* parser, const char* sentence)
{
    nmea_gga_t* gga = &parser->data.gga;
    memset(gga, 0, sizeof(nmea_gga_t));

    /* Field 0 is the sentence type tag (e.g. "GNGGA"), data fields start at 1.
     * GGA format: GNGGA,time,lat,N/S,lon,E/W,fix,sats,HDOP,alt,M,geoSep,M,ageDiff,dgpsId */
    uint8_t flen;
    uint8_t coord_flen;  /* separate length for coordinate fields */
    const char* f;
    const char* hemi_f;
    char hemi;

    f = nmea_get_field(sentence, 1, &flen);
    gga->utc_time = nmea_parse_double(f, flen);

    f = nmea_get_field(sentence, 2, &coord_flen);  /* lat */
    hemi_f = nmea_get_field(sentence, 3, &flen);      /* N/S */
    hemi = nmea_parse_char(hemi_f, flen);
    gga->latitude = nmea_parse_latlon(f, coord_flen, hemi);

    f = nmea_get_field(sentence, 4, &coord_flen);  /* lon */
    hemi_f = nmea_get_field(sentence, 5, &flen);      /* E/W */
    hemi = nmea_parse_char(hemi_f, flen);
    gga->longitude = nmea_parse_latlon(f, coord_flen, hemi);

    f = nmea_get_field(sentence, 6, &flen);
    gga->fix_quality = (uint8_t)nmea_parse_int(f, flen);

    f = nmea_get_field(sentence, 7, &flen);
    gga->num_sats = (uint8_t)nmea_parse_int(f, flen);

    f = nmea_get_field(sentence, 8, &flen);
    gga->hdop = nmea_parse_double(f, flen);

    f = nmea_get_field(sentence, 9, &flen);
    gga->altitude = nmea_parse_double(f, flen);
    /* Field 10 is altitude unit ('M'), skip */

    f = nmea_get_field(sentence, 11, &flen);
    gga->geoid_sep = nmea_parse_double(f, flen);
    /* Field 12 is geoid unit ('M'), skip */

    f = nmea_get_field(sentence, 13, &flen);
    if (f != NULL && flen > 0) {
        gga->age_diff = nmea_parse_double(f, flen);
        gga->age_diff_valid = true;
    } else {
        gga->age_diff = 0.0;
        gga->age_diff_valid = false;
    }

    f = nmea_get_field(sentence, 14, &flen);
    gga->diff_station_id = (uint16_t)nmea_parse_int(f, flen);
}

static void nmea_parse_rmc(nmea_parser_t* parser, const char* sentence)
{
    nmea_rmc_t* rmc = &parser->data.rmc;
    memset(rmc, 0, sizeof(nmea_rmc_t));

    /* Field 0 is the sentence type tag (e.g. "GNRMC"), data fields start at 1.
     * RMC format: GNRMC,time,status,lat,N/S,lon,E/W,speed,course,date,magVar,magDir,mode */
    uint8_t flen;
    uint8_t coord_flen;  /* separate length for coordinate fields */
    const char* f;
    const char* hemi_f;
    char hemi;

    f = nmea_get_field(sentence, 1, &flen);
    rmc->utc_time = nmea_parse_double(f, flen);

    f = nmea_get_field(sentence, 2, &flen);
    rmc->status_valid = (flen > 0 && f[0] == 'A');

    f = nmea_get_field(sentence, 3, &coord_flen);  /* lat */
    hemi_f = nmea_get_field(sentence, 4, &flen);      /* N/S */
    hemi = nmea_parse_char(hemi_f, flen);
    rmc->latitude = nmea_parse_latlon(f, coord_flen, hemi);

    f = nmea_get_field(sentence, 5, &coord_flen);  /* lon */
    hemi_f = nmea_get_field(sentence, 6, &flen);      /* E/W */
    hemi = nmea_parse_char(hemi_f, flen);
    rmc->longitude = nmea_parse_latlon(f, coord_flen, hemi);

    f = nmea_get_field(sentence, 7, &flen);
    rmc->speed_knots = nmea_parse_double(f, flen);

    f = nmea_get_field(sentence, 8, &flen);
    rmc->course_true = nmea_parse_double(f, flen);

    f = nmea_get_field(sentence, 9, &flen);
    nmea_parse_date(f, flen, &rmc->date_day, &rmc->date_month, &rmc->date_year);

    f = nmea_get_field(sentence, 10, &flen);
    rmc->mag_variation = nmea_parse_double(f, flen);

    f = nmea_get_field(sentence, 11, &flen);
    rmc->mag_var_dir = nmea_parse_char(f, flen);

    f = nmea_get_field(sentence, 12, &flen);
    rmc->mode = nmea_parse_char(f, flen);
}

static void nmea_parse_gst(nmea_parser_t* parser, const char* sentence)
{
    nmea_gst_t* gst = &parser->data.gst;
    memset(gst, 0, sizeof(nmea_gst_t));

    /* Field 0 is the sentence type tag (e.g. "GNGST"), data fields start at 1.
     * GST format: GNGST,time,rms,major,minor,orient,stdLat,stdLon,stdAlt */
    uint8_t flen;
    const char* f;

    f = nmea_get_field(sentence, 1, &flen);
    gst->utc_time = nmea_parse_double(f, flen);

    f = nmea_get_field(sentence, 2, &flen);
    gst->total_rms = nmea_parse_double(f, flen);

    f = nmea_get_field(sentence, 3, &flen);
    gst->std_major = nmea_parse_double(f, flen);

    f = nmea_get_field(sentence, 4, &flen);
    gst->std_minor = nmea_parse_double(f, flen);

    f = nmea_get_field(sentence, 5, &flen);
    gst->orientation = nmea_parse_double(f, flen);

    f = nmea_get_field(sentence, 6, &flen);
    gst->std_lat = nmea_parse_double(f, flen);

    f = nmea_get_field(sentence, 7, &flen);
    gst->std_lon = nmea_parse_double(f, flen);

    f = nmea_get_field(sentence, 8, &flen);
    gst->std_alt = nmea_parse_double(f, flen);
}

static void nmea_parse_gsv(nmea_parser_t* parser, const char* sentence)
{
    nmea_gsv_t* gsv = &parser->data.gsv;
    memset(gsv, 0, sizeof(nmea_gsv_t));

    /* Initialize SNR to -1 (not available) */
    for (int i = 0; i < NMEA_GSV_MAX_SATS; i++) {
        gsv->sats[i].snr = -1;
    }

    uint8_t flen;
    const char* f;

    /* Field 0 is the sentence type tag (e.g. "GNGSV"), data fields start at 1.
     * GSV format: GNGSV,numMsgs,msgNum,numSats,PRN1,el1,az1,SNR1,... */
    f = nmea_get_field(sentence, 1, &flen);
    gsv->num_messages = (uint8_t)nmea_parse_int(f, flen);

    f = nmea_get_field(sentence, 2, &flen);
    gsv->message_number = (uint8_t)nmea_parse_int(f, flen);

    f = nmea_get_field(sentence, 3, &flen);
    gsv->num_sats_in_view = (uint8_t)nmea_parse_int(f, flen);

    /* Each satellite has 4 fields: PRN, elevation, azimuth, SNR */
    gsv->sat_count = 0;
    for (int i = 0; i < NMEA_GSV_MAX_SATS; i++) {
        uint8_t base = 4 + i * 4;

        f = nmea_get_field(sentence, base, &flen);
        if (f == NULL || flen == 0) break;
        gsv->sats[i].prn = nmea_parse_int(f, flen);

        f = nmea_get_field(sentence, base + 1, &flen);
        gsv->sats[i].elevation = nmea_parse_int(f, flen);

        f = nmea_get_field(sentence, base + 2, &flen);
        gsv->sats[i].azimuth = nmea_parse_int(f, flen);

        f = nmea_get_field(sentence, base + 3, &flen);
        if (flen > 0) {
            gsv->sats[i].snr = nmea_parse_int(f, flen);
        }

        gsv->sat_count++;
    }

    /* Optional: system ID (NMEA 4.10+) */
    f = nmea_get_field(sentence, 4 + NMEA_GSV_MAX_SATS * 4, &flen);
    if (f != NULL && flen > 0) {
        gsv->system_id = (uint8_t)nmea_parse_int(f, flen);
    }
}

static void nmea_parse_gsa(nmea_parser_t* parser, const char* sentence)
{
    nmea_gsa_t* gsa = &parser->data.gsa;
    memset(gsa, 0, sizeof(nmea_gsa_t));

    uint8_t flen;
    const char* f;

    /* Field 0 is the sentence type tag (e.g. "GNGSA"), data fields start at 1.
     * GSA format: GNGSA,mode,fix,PRN1..12,PDOP,HDOP,VDOP[,sysId] */
    f = nmea_get_field(sentence, 1, &flen);
    gsa->mode = nmea_parse_char(f, flen);

    f = nmea_get_field(sentence, 2, &flen);
    gsa->fix = (uint8_t)nmea_parse_int(f, flen);

    /* PRNs: fields 3 through 14 (12 satellites) */
    for (int i = 0; i < NMEA_GSA_MAX_SATS; i++) {
        f = nmea_get_field(sentence, 3 + i, &flen);
        gsa->prn[i] = nmea_parse_int(f, flen);
    }

    f = nmea_get_field(sentence, 15, &flen);
    gsa->pdop = nmea_parse_double(f, flen);

    f = nmea_get_field(sentence, 16, &flen);
    gsa->hdop = nmea_parse_double(f, flen);

    f = nmea_get_field(sentence, 17, &flen);
    gsa->vdop = nmea_parse_double(f, flen);

    /* Optional: system ID (NMEA 4.10+) */
    f = nmea_get_field(sentence, 18, &flen);
    if (f != NULL && flen > 0) {
        gsa->system_id = (uint8_t)nmea_parse_int(f, flen);
    }
}

/* ---- Sentence finalization ---- */

static void nmea_finalize_sentence(nmea_parser_t* parser)
{
    parser->type = nmea_identify_sentence(parser->buffer);

    switch (parser->type) {
    case NMEA_SENTENCE_GGA:
        nmea_parse_gga(parser, parser->buffer);
        break;
    case NMEA_SENTENCE_RMC:
        nmea_parse_rmc(parser, parser->buffer);
        break;
    case NMEA_SENTENCE_GST:
        nmea_parse_gst(parser, parser->buffer);
        break;
    case NMEA_SENTENCE_GSV:
        nmea_parse_gsv(parser, parser->buffer);
        break;
    case NMEA_SENTENCE_GSA:
        nmea_parse_gsa(parser, parser->buffer);
        break;
    default:
        break;
    }
}

/* ---- Public API ---- */

void nmea_parser_init(nmea_parser_t* parser)
{
    memset(parser, 0, sizeof(nmea_parser_t));
    parser->state = NMEA_STATE_IDLE;
}

nmea_result_t nmea_parser_feed(nmea_parser_t* parser, uint8_t byte)
{
    switch (parser->state) {

    case NMEA_STATE_IDLE:
        if (byte == '$') {
            parser->index = 0;
            parser->calc_checksum = 0;
            parser->result = NMEA_RESULT_INCOMPLETE;
            parser->type = NMEA_SENTENCE_NONE;
            parser->state = NMEA_STATE_DATA;
        }
        return NMEA_RESULT_INCOMPLETE;

    case NMEA_STATE_DATA:
        if (byte == '*') {
            /* End of data, start of checksum */
            parser->buffer[parser->index] = '\0';
            parser->state = NMEA_STATE_CHECKSUM_H;
            return NMEA_RESULT_INCOMPLETE;
        }
        if (byte == '\r' || byte == '\n') {
            /* Unexpected line ending without checksum - invalid sentence */
            parser->state = NMEA_STATE_IDLE;
            return NMEA_RESULT_INVALID_CHECKSUM;
        }
        if (parser->index < NMEA_MAX_SENTENCE_LEN - 1) {
            parser->buffer[parser->index++] = (char)byte;
            parser->calc_checksum ^= byte;
        } else {
            /* Buffer overflow */
            parser->state = NMEA_STATE_IDLE;
            parser->result = NMEA_RESULT_OVERFLOW;
            return NMEA_RESULT_OVERFLOW;
        }
        return NMEA_RESULT_INCOMPLETE;

    case NMEA_STATE_CHECKSUM_H: {
        int val = nmea_hex_val(byte);
        if (val < 0) {
            parser->state = NMEA_STATE_IDLE;
            return NMEA_RESULT_INVALID_CHECKSUM;
        }
        parser->recv_checksum_hi = (uint8_t)val;
        parser->state = NMEA_STATE_CHECKSUM_L;
        return NMEA_RESULT_INCOMPLETE;
    }

    case NMEA_STATE_CHECKSUM_L: {
        int val = nmea_hex_val(byte);
        if (val < 0) {
            parser->state = NMEA_STATE_IDLE;
            return NMEA_RESULT_INVALID_CHECKSUM;
        }
        parser->recv_checksum_lo = (uint8_t)val;
        uint8_t received = (parser->recv_checksum_hi << 4) | parser->recv_checksum_lo;

        if (received == parser->calc_checksum) {
            parser->result = NMEA_RESULT_VALID;
            nmea_finalize_sentence(parser);
        } else {
            parser->result = NMEA_RESULT_INVALID_CHECKSUM;
            /* CRITICAL: Do NOT parse data from invalid sentences.
             * Clear type so downstream consumers cannot accidentally
             * use stale data from parser->data. */
            parser->type = NMEA_SENTENCE_NONE;
        }
        parser->state = NMEA_STATE_CR;
        return parser->result;
    }

    case NMEA_STATE_CR:
        if (byte == '\r') {
            parser->state = NMEA_STATE_LF;
        } else {
            /* Tolerate missing CR, accept directly */
            if (byte == '\n') {
                parser->state = NMEA_STATE_IDLE;
            } else {
                parser->state = NMEA_STATE_IDLE;
            }
        }
        /* Result already returned in CHECKSUM_L state */
        return NMEA_RESULT_INCOMPLETE;

    case NMEA_STATE_LF:
        parser->state = NMEA_STATE_IDLE;
        return NMEA_RESULT_INCOMPLETE;

    default:
        parser->state = NMEA_STATE_IDLE;
        return NMEA_RESULT_INCOMPLETE;
    }
}
