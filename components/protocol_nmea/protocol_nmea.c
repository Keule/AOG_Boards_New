#include "protocol_nmea.h"

#include <stdlib.h>
#include <string.h>

static bool starts_with(const char* line, const char* prefix)
{
    size_t n = strlen(prefix);
    return line != 0 && strncmp(line, prefix, n) == 0;
}

static double parse_latlon(const char* token, const char hemi)
{
    double raw = atof(token);
    int degrees = (int)(raw / 100.0);
    double minutes = raw - (degrees * 100.0);
    double value = degrees + (minutes / 60.0);

    if (hemi == 'S' || hemi == 'W') {
        value = -value;
    }

    return value;
}

static bool parse_gga(char* line, nmea_solution_t* out)
{
    char* fields[16] = {0};
    int i = 0;
    int count = 0;

    for (i = 0; line[i] != '\0' && count < 16; ++i) {
        if (i == 0 || line[i - 1] == ',') {
            fields[count++] = &line[i];
        }
        if (line[i] == ',') {
            line[i] = '\0';
        }
    }

    if (count < 10 || fields[2][0] == '\0' || fields[4][0] == '\0') {
        return false;
    }

    out->latitude_deg = parse_latlon(fields[2], fields[3][0]);
    out->longitude_deg = parse_latlon(fields[4], fields[5][0]);
    out->has_fix = (fields[6][0] > '0');
    out->altitude_m = (float)atof(fields[9]);
    return true;
}

static bool parse_rmc(char* line, nmea_solution_t* out)
{
    char* fields[16] = {0};
    int i = 0;
    int count = 0;

    for (i = 0; line[i] != '\0' && count < 16; ++i) {
        if (i == 0 || line[i - 1] == ',') {
            fields[count++] = &line[i];
        }
        if (line[i] == ',') {
            line[i] = '\0';
        }
    }

    if (count < 9) {
        return false;
    }

    if (fields[2][0] == 'A') {
        out->has_fix = true;
    }

    out->speed_knots = (float)atof(fields[7]);
    out->course_deg = (float)atof(fields[8]);
    return true;
}

static bool parse_gst(char* line, nmea_solution_t* out)
{
    char* fields[16] = {0};
    int i = 0;
    int count = 0;

    for (i = 0; line[i] != '\0' && count < 16; ++i) {
        if (i == 0 || line[i - 1] == ',') {
            fields[count++] = &line[i];
        }
        if (line[i] == ',') {
            line[i] = '\0';
        }
    }

    if (count < 9) {
        return false;
    }

    out->sigma_lat_m = (float)atof(fields[6]);
    out->sigma_lon_m = (float)atof(fields[7]);
    out->sigma_alt_m = (float)atof(fields[8]);
    return true;
}

bool protocol_nmea_parse_line(const char* line, nmea_solution_t* in_out_solution)
{
    char local[128];

    if (line == 0 || in_out_solution == 0) {
        return false;
    }

    strncpy(local, line, sizeof(local) - 1U);
    local[sizeof(local) - 1U] = '\0';

    if (starts_with(local, "$GPGGA") || starts_with(local, "$GNGGA")) {
        return parse_gga(local, in_out_solution);
    }

    if (starts_with(local, "$GPRMC") || starts_with(local, "$GNRMC")) {
        return parse_rmc(local, in_out_solution);
    }

    if (starts_with(local, "$GPGST") || starts_with(local, "$GNGST")) {
        return parse_gst(local, in_out_solution);
    }

    return false;
}
