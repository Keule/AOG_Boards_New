#include "gnss_dual_heading.h"

#include <string.h>

static void gnss_dual_heading_component_step(runtime_component_t* component)
{
    gnss_dual_heading_t* instance = 0;
    if (component == 0) {
        return;
    }

    instance = (gnss_dual_heading_t*)component->user_data;
    gnss_dual_heading_step(instance);
}

void gnss_dual_heading_init(gnss_dual_heading_t* instance, const gnss_um980_t* primary, const gnss_um980_t* secondary)
{
    if (instance == 0) {
        return;
    }

    memset(instance, 0, sizeof(*instance));
    instance->primary = primary;
    instance->secondary = secondary;
    instance->component.name = "gnss_dual_heading";
    instance->component.user_data = instance;
    instance->component.step = gnss_dual_heading_component_step;
}

void gnss_dual_heading_step(gnss_dual_heading_t* instance)
{
    gnss_snapshot_t p;
    gnss_snapshot_t s;

    if (instance == 0) {
        return;
    }

    if (!gnss_um980_get_snapshot(instance->primary, &p) || !gnss_um980_get_snapshot(instance->secondary, &s)) {
        return;
    }

    instance->snapshot.valid = true;
    instance->snapshot.heading_deg = (s.longitude_deg >= p.longitude_deg) ? 90.0f : 270.0f;
    instance->snapshot.timestamp_ms = (p.timestamp_ms > s.timestamp_ms) ? p.timestamp_ms : s.timestamp_ms;
}

bool gnss_dual_heading_get_snapshot(const gnss_dual_heading_t* instance, heading_snapshot_t* out_snapshot)
{
    if (instance == 0 || out_snapshot == 0 || !instance->snapshot.valid) {
        return false;
    }

    *out_snapshot = instance->snapshot;
    return true;
}

runtime_component_t* gnss_dual_heading_component(gnss_dual_heading_t* instance)
{
    return (instance != 0) ? &instance->component : 0;
}
