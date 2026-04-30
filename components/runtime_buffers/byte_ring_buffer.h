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

/* Peek at data in the ring buffer without removing it.
 * Copies min(length, available) bytes into out. Returns bytes copied. */
size_t byte_ring_buffer_peek(const byte_ring_buffer_t* buffer, uint8_t* out, size_t length);

/* Remove (consume) bytes from the head of the ring buffer without copying.
 * Useful after peek + HAL write to discard already-written bytes.
 * Returns bytes consumed. */
size_t byte_ring_buffer_consume(byte_ring_buffer_t* buffer, size_t length);

#ifdef __cplusplus
}
#endif
