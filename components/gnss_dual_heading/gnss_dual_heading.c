#include "gnss_dual_heading.h"
#include "gnss_um980.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void gnss_dual_heading_init(gnss_dual_heading_calc_t* calc)
{
    if (calc == NULL) {
        return;
    }
    memset(calc, 0, sizeof(gnss_dual_heading_calc_t));
    snapshot_buffer_init(&calc->heading_snapshot,
                        &calc->heading_storage,
                        sizeof(gnss_dual_heading_t));
    calc->component.service_step = gnss_dual_heading_service_step;
}

void gnss_dual_heading_set_sources(gnss_dual_heading_calc_t* calc,
                                    gnss_um980_t* primary,
                                    gnss_um980_t* secondary)
{
    if (calc == NULL) {
        return;
    }
    calc->primary = primary;
    calc->secondary = secondary;
}

void gnss_dual_heading_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    (void)timestamp_us;
    gnss_dual_heading_calc_t* calc = (gnss_dual_heading_calc_t*)comp;
    if (calc == NULL || calc->primary == NULL || calc->secondary == NULL) {
        return;
    }

    const nmea_gga_t* primary_gga = gnss_um980_get_gga(calc->primary);
    const nmea_gga_t* secondary_gga = gnss_um980_get_gga(calc->secondary);

    if (primary_gga == NULL || secondary_gga == NULL) {
        calc->result.valid = false;
        return;
    }

    calc->result.primary_fix = primary_gga->fix_quality;
    calc->result.secondary_fix = secondary_gga->fix_quality;

    /* Both receivers must have a fix */
    if (primary_gga->fix_quality == 0 || secondary_gga->fix_quality == 0) {
        calc->result.valid = false;
        return;
    }

    /* Calculate heading from two positions using atan2 */
    double dlat = secondary_gga->latitude - primary_gga->latitude;
    double dlon = secondary_gga->longitude - primary_gga->longitude;

    if (dlat == 0.0 && dlon == 0.0) {
        calc->result.valid = false;
        return;
    }

    double lat_rad = primary_gga->latitude * M_PI / 180.0;
    double east  = dlon * cos(lat_rad);
    double north = dlat;

    calc->result.heading_rad = atan2(east, north);
    if (calc->result.heading_rad < 0.0) {
        calc->result.heading_rad += 2.0 * M_PI;
    }

    calc->result.valid = true;
    calc->calc_count++;

    /* Publish to snapshot buffer for downstream consumers */
    snapshot_buffer_set(&calc->heading_snapshot, &calc->result);
}

const gnss_dual_heading_t* gnss_dual_heading_get(const gnss_dual_heading_calc_t* calc)
{
    if (calc == NULL) {
        return NULL;
    }
    return &calc->result;
}

const snapshot_buffer_t* gnss_dual_heading_get_snapshot(const gnss_dual_heading_calc_t* calc)
{
    if (calc == NULL) {
        return NULL;
    }
    return &calc->heading_snapshot;
}
