#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "runtime_component.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool valid;
    int32_t latitude_e7;
    int32_t longitude_e7;
    int32_t altitude_mm;
    bool has_gga;
    bool has_rmc;
    bool has_gst;
    bool has_gsv;
    bool has_gsa;
} gnss_um980_receiver_data_t;

int gnss_um980_init(void);
runtime_component_t* gnss_um980_component(void);
const gnss_um980_receiver_data_t* gnss_um980_primary(void);
const gnss_um980_receiver_data_t* gnss_um980_secondary(void);

#ifdef __cplusplus
}
#endif
