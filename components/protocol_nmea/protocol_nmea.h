#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool has_fix;
    double latitude_deg;
    double longitude_deg;
    float altitude_m;
    float speed_knots;
    float course_deg;
    float sigma_lat_m;
    float sigma_lon_m;
    float sigma_alt_m;
} nmea_solution_t;

bool protocol_nmea_parse_line(const char* line, nmea_solution_t* in_out_solution);

#ifdef __cplusplus
}
#endif
