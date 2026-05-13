#pragma once

#include <stdint.h>
#include "gnss_snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    gnss_snapshot_t buffers[2];
    uint32_t write_idx;
    uint32_t read_idx;
} gnss_cache_t;

void gnss_cache_init(gnss_cache_t* cache);
void gnss_cache_publish(gnss_cache_t* cache, const gnss_snapshot_t* snapshot);
const gnss_snapshot_t* gnss_cache_read(const gnss_cache_t* cache);

#ifdef __cplusplus
}
#endif

