#pragma once

#include <stddef.h>
#include <stdint.h>

#include "runtime_component.h"

#ifdef __cplusplus
extern "C" {
#endif

int rtcm_router_init(void);
runtime_component_t* rtcm_router_component(void);
void rtcm_router_push_from_ntrip(const uint8_t* data, size_t length, uint64_t now_us);

#ifdef __cplusplus
}
#endif
