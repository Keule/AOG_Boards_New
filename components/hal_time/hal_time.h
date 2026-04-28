#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint64_t hal_time_us(void);
uint32_t hal_time_ms(void);

#ifdef __cplusplus
}
#endif
