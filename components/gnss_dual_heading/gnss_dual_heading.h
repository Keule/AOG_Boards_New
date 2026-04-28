#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "runtime_component.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool valid;
    int32_t heading_mdeg;
} gnss_dual_heading_data_t;

int gnss_dual_heading_init(void);
runtime_component_t* gnss_dual_heading_component(void);
const gnss_dual_heading_data_t* gnss_dual_heading_get(void);

#ifdef __cplusplus
}
#endif
