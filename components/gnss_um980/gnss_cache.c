#include <stddef.h>
#include "gnss_cache.h"


void gnss_cache_init(gnss_cache_t* cache)
{
    if (cache == NULL) {
        return;
    }

    gnss_snapshot_init(&cache->buffers[0]);
    gnss_snapshot_init(&cache->buffers[1]);
    __atomic_store_n(&cache->write_idx, 0U, __ATOMIC_RELAXED);
    __atomic_store_n(&cache->read_idx, 0U, __ATOMIC_RELAXED);
}

void gnss_cache_publish(gnss_cache_t* cache, const gnss_snapshot_t* snapshot)
{
    if (cache == NULL || snapshot == NULL) {
        return;
    }

    uint32_t write_idx = __atomic_load_n(&cache->write_idx, __ATOMIC_RELAXED);
    cache->buffers[write_idx] = *snapshot;
    __atomic_store_n(&cache->read_idx, write_idx, __ATOMIC_RELEASE);
    __atomic_store_n(&cache->write_idx, write_idx ^ 1U, __ATOMIC_RELAXED);
}

const gnss_snapshot_t* gnss_cache_read(const gnss_cache_t* cache)
{
    if (cache == NULL) {
        return NULL;
    }

    uint32_t read_idx = __atomic_load_n((uint32_t*)&cache->read_idx, __ATOMIC_ACQUIRE);
    return &cache->buffers[read_idx];
}

