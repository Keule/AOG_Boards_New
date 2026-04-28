#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t* storage;
    size_t value_size;
    bool valid;
    uint32_t sequence;
} snapshot_buffer_t;

void snapshot_buffer_init(snapshot_buffer_t* buffer, void* storage, size_t value_size);
void snapshot_buffer_set(snapshot_buffer_t* buffer, const void* value);
bool snapshot_buffer_get(const snapshot_buffer_t* buffer, void* out_value);
bool snapshot_buffer_is_valid(const snapshot_buffer_t* buffer);
uint32_t snapshot_buffer_sequence(const snapshot_buffer_t* buffer);

#ifdef __cplusplus
}
#endif
