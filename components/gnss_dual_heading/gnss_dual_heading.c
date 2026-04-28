#include "gnss_dual_heading.h"

#include <math.h>

#include "gnss_um980.h"

static gnss_dual_heading_data_t s_heading;

static void gnss_dual_heading_fast_process(runtime_component_t* component, const fast_cycle_context_t* ctx)
{
    const gnss_um980_receiver_data_t* p = gnss_um980_primary();
    const gnss_um980_receiver_data_t* s = gnss_um980_secondary();
    double lat1 = 0.0;
    double lon1 = 0.0;
    double lat2 = 0.0;
    double lon2 = 0.0;
    double heading_rad = 0.0;

    (void)component;
    (void)ctx;

    if (!p->valid || !s->valid) {
        s_heading.valid = false;
        return;
    }

    lat1 = p->latitude_e7 / 10000000.0;
    lon1 = p->longitude_e7 / 10000000.0;
    lat2 = s->latitude_e7 / 10000000.0;
    lon2 = s->longitude_e7 / 10000000.0;

    heading_rad = atan2((lon2 - lon1), (lat2 - lat1));
    s_heading.heading_mdeg = (int32_t)(heading_rad * (180.0 / 3.14159265358979323846) * 1000.0);
    s_heading.valid = true;
}

static runtime_component_t s_component = {
    .name = "gnss_dual_heading",
    .user_data = 0,
    .fast_input = 0,
    .fast_process = gnss_dual_heading_fast_process,
    .fast_output = 0,
};

int gnss_dual_heading_init(void)
{
    s_heading = (gnss_dual_heading_data_t){0};
    return 0;
}

runtime_component_t* gnss_dual_heading_component(void)
{
    return &s_component;
}

const gnss_dual_heading_data_t* gnss_dual_heading_get(void)
{
    return &s_heading;
}
