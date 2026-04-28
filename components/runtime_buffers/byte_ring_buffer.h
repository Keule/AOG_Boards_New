#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t* storage;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t size;
} byte_ring_buffer_t;

void byte_ring_buffer_init(byte_ring_buffer_t* buffer, uint8_t* storage, size_t capacity);
size_t byte_ring_buffer_push(byte_ring_buffer_t* buffer, const uint8_t* data, size_t length);
size_t byte_ring_buffer_pop(byte_ring_buffer_t* buffer, uint8_t* out_data, size_t max_length);
size_t byte_ring_buffer_peek(const byte_ring_buffer_t* buffer, uint8_t* out_data, size_t max_length);
size_t byte_ring_buffer_size(const byte_ring_buffer_t* buffer);
bool byte_ring_buffer_is_empty(const byte_ring_buffer_t* buffer);

#ifdef __cplusplus
}
#endif
