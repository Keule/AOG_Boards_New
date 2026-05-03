#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- NMEA Sentence Types ---- */

typedef enum {
    NMEA_SENTENCE_NONE = 0,
    NMEA_SENTENCE_GGA,
    NMEA_SENTENCE_RMC,
    NMEA_SENTENCE_GST,
    NMEA_SENTENCE_GSV,
    NMEA_SENTENCE_GSA
} nmea_sentence_type_t;

/* ---- Parser Result ---- */

typedef enum {
    NMEA_RESULT_INCOMPLETE = 0,     /* Still accumulating */
    NMEA_RESULT_VALID,              /* Complete with valid checksum */
    NMEA_RESULT_INVALID_CHECKSUM,   /* Complete with invalid checksum */
    NMEA_RESULT_OVERFLOW,           /* Sentence too long for buffer */
    NMEA_RESULT_BINARY_REJECT       /* Non-printable byte in sentence data */
} nmea_result_t;

/* ---- NMEA Data Structures ---- */

/* GGA - Global Positioning System Fix Data */
typedef struct {
    double utc_time;            /* hhmmss.ss */
    double latitude;            /* decimal degrees (N positive) */
    double longitude;           /* decimal degrees (E positive) */
    uint8_t fix_quality;        /* 0=none, 1=GPS/3D, 2=DGPS, 4=RTK_FIX, 5=RTK_FLOAT */
    uint8_t num_sats;           /* satellites in use */
    double hdop;                /* horizontal dilution of precision */
    double altitude;            /* above geoid (meters) */
    double geoid_sep;           /* geoid separation (meters) */
    double age_diff;            /* age of differential correction (seconds) */
    bool    age_diff_valid;     /* true if age_diff field was present and non-empty */
    uint16_t diff_station_id;   /* differential reference station ID */
} nmea_gga_t;

/* RMC - Recommended Minimum Navigation Data */
typedef struct {
    double utc_time;            /* hhmmss.ss */
    bool   status_valid;        /* true = active (A), false = void (V) */
    double latitude;            /* decimal degrees (N positive) */
    double longitude;           /* decimal degrees (E positive) */
    double speed_knots;         /* speed over ground (knots) */
    double course_true;         /* course over ground (degrees true) */
    uint8_t date_day;
    uint8_t date_month;
    uint16_t date_year;         /* full year (e.g. 2024) */
    double mag_variation;       /* magnetic variation (degrees) */
    char   mag_var_dir;         /* 'E' or 'W' */
    char   mode;                /* 'A'=autonomous, 'D'=differential, 'E'=estimated, 'N'=data not valid */
} nmea_rmc_t;

/* GST - GPS Pseudorange Noise Statistics */
typedef struct {
    double utc_time;            /* hhmmss.ss */
    double total_rms;           /* total RMS std dev (meters) */
    double std_major;           /* semi-major axis std dev (meters) */
    double std_minor;           /* semi-minor axis std dev (meters) */
    double orientation;         /* orientation of semi-major axis (degrees) */
    double std_lat;             /* latitude error 1-sigma (meters) */
    double std_lon;             /* longitude error 1-sigma (meters) */
    double std_alt;             /* altitude error 1-sigma (meters) */
} nmea_gst_t;

/* GSV - Satellites in View */
#define NMEA_GSV_MAX_SATS  4

typedef struct {
    int prn;                    /* satellite PRN number */
    int elevation;              /* elevation (degrees), -1 if N/A */
    int azimuth;                /* azimuth (degrees), -1 if N/A */
    int snr;                    /* C/N0 (dB-Hz), -1 if N/A */
} nmea_gsv_sat_t;

typedef struct {
    uint8_t num_messages;       /* total number of GSV messages (1-3) */
    uint8_t message_number;     /* this message number (1-3) */
    uint8_t num_sats_in_view;   /* total satellites in view */
    uint8_t sat_count;          /* satellites in this message (0-4) */
    nmea_gsv_sat_t sats[NMEA_GSV_MAX_SATS];
    uint8_t system_id;          /* GNSS system ID (1=GPS, 2=GLONASS, etc.), 0 if not present */
} nmea_gsv_t;

/* GSA - DOP and Active Satellites */
#define NMEA_GSA_MAX_SATS  12

typedef struct {
    char    mode;               /* 'A'=auto, 'M'=manual */
    uint8_t fix;                /* 1=no fix, 2=2D, 3=3D */
    int     prn[NMEA_GSA_MAX_SATS]; /* PRN numbers (0 = unused slot) */
    double  pdop;               /* position dilution of precision */
    double  hdop;               /* horizontal dilution of precision */
    double  vdop;               /* vertical dilution of precision */
    uint8_t system_id;          /* GNSS system ID (1=GPS, 2=GLONASS, etc.), 0 if not present */
} nmea_gsa_t;

/* ---- Parser State Machine ---- */

typedef enum {
    NMEA_STATE_IDLE = 0,        /* Waiting for '$' */
    NMEA_STATE_DATA,            /* Accumulating data chars */
    NMEA_STATE_CHECKSUM_H,      /* First hex digit of checksum */
    NMEA_STATE_CHECKSUM_L,      /* Second hex digit of checksum */
    NMEA_STATE_CR,              /* Waiting for \r */
    NMEA_STATE_LF               /* Waiting for \n */
} nmea_state_t;

#define NMEA_MAX_SENTENCE_LEN  128

/* Parser structure */
typedef struct {
    nmea_state_t state;
    char    buffer[NMEA_MAX_SENTENCE_LEN];
    uint8_t index;
    uint8_t calc_checksum;
    uint8_t recv_checksum_hi;
    uint8_t recv_checksum_lo;
    /* Result */
    nmea_result_t       result;
    nmea_sentence_type_t type;
    union {
        nmea_gga_t gga;
        nmea_rmc_t rmc;
        nmea_gst_t gst;
        nmea_gsv_t gsv;
        nmea_gsa_t gsa;
    } data;

    /* ---- NAV-GNSS-NMEA-CORRUPTION-001: Diagnostic counters ---- */
    uint32_t binary_rejects;       /* binary byte detected mid-sentence */
    uint32_t garbage_discarded;    /* non-'$' bytes discarded in IDLE */
    uint32_t malformed_csum;       /* non-hex byte after '*' */

    /* ---- Last bad line capture (for rate-limited diagnostic) ---- */
    uint8_t  bad_line[NMEA_MAX_SENTENCE_LEN];
    uint8_t  bad_line_len;
    uint8_t  bad_line_reason;      /* 0=binary, 1=checksum, 2=overflow, 3=malformed_csum */
    uint8_t  bad_parsed_csum;      /* parsed checksum hex value (for checksum failures) */
    uint8_t  bad_computed_csum;    /* computed checksum (for checksum failures) */
} nmea_parser_t;

/* ---- API ---- */

/* Initialize parser */
void nmea_parser_init(nmea_parser_t* parser);

/* Feed one byte into the streaming parser.
 * Returns NMEA_RESULT_VALID or NMEA_RESULT_INVALID_CHECKSUM when a complete
 * sentence is received. Caller reads result from parser->type and parser->data.
 * After reading, the parser is ready for the next sentence. */
nmea_result_t nmea_parser_feed(nmea_parser_t* parser, uint8_t byte);

#ifdef __cplusplus
}
#endif
