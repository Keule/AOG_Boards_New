#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t* data;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t size;
    uint32_t overflow_count;
} byte_ring_buffer_t;

void byte_ring_buffer_init(byte_ring_buffer_t* buffer, uint8_t* storage, size_t capacity);
size_t byte_ring_buffer_write(byte_ring_buffer_t* buffer, const uint8_t* data, size_t length);
size_t byte_ring_buffer_read(byte_ring_buffer_t* buffer, uint8_t* out, size_t length);
size_t byte_ring_buffer_available(const byte_ring_buffer_t* buffer);
uint32_t byte_ring_buffer_overflow_count(const byte_ring_buffer_t* buffer);

#ifdef __cplusplus
}
#endif
