#pragma once

#include <stdint.h>

typedef struct {
    uint64_t cycle_id;
    uint64_t timestamp_us;
    uint32_t period_us;
} fast_cycle_context_t;
