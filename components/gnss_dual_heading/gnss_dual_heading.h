#pragma once

#include <stdbool.h>

#include "gnss_um980.h"
#include "runtime_component.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool valid;
    float heading_deg;
    uint64_t timestamp_ms;
} heading_snapshot_t;

typedef struct {
    const gnss_um980_t* primary;
    const gnss_um980_t* secondary;
    heading_snapshot_t snapshot;
    runtime_component_t component;
} gnss_dual_heading_t;

void gnss_dual_heading_init(gnss_dual_heading_t* instance, const gnss_um980_t* primary, const gnss_um980_t* secondary);
void gnss_dual_heading_step(gnss_dual_heading_t* instance);
bool gnss_dual_heading_get_snapshot(const gnss_dual_heading_t* instance, heading_snapshot_t* out_snapshot);
runtime_component_t* gnss_dual_heading_component(gnss_dual_heading_t* instance);

#ifdef __cplusplus
}
#endif
