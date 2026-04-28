#include "byte_ring_buffer.h"

void byte_ring_buffer_init(byte_ring_buffer_t* buffer, uint8_t* storage, size_t capacity)
{
    if (buffer == 0 || storage == 0 || capacity == 0) {
        return;
    }

    buffer->storage = storage;
    buffer->capacity = capacity;
    buffer->head = 0;
    buffer->tail = 0;
    buffer->size = 0;
}

size_t byte_ring_buffer_push(byte_ring_buffer_t* buffer, const uint8_t* data, size_t length)
{
    size_t i = 0;
    if (buffer == 0 || data == 0) {
        return 0;
    }

    for (i = 0; i < length && buffer->size < buffer->capacity; ++i) {
        buffer->storage[buffer->head] = data[i];
        buffer->head = (buffer->head + 1U) % buffer->capacity;
        buffer->size++;
    }

    return i;
}

size_t byte_ring_buffer_pop(byte_ring_buffer_t* buffer, uint8_t* out_data, size_t max_length)
{
    size_t i = 0;
    if (buffer == 0 || out_data == 0) {
        return 0;
    }

    for (i = 0; i < max_length && buffer->size > 0; ++i) {
        out_data[i] = buffer->storage[buffer->tail];
        buffer->tail = (buffer->tail + 1U) % buffer->capacity;
        buffer->size--;
    }

    return i;
}

size_t byte_ring_buffer_peek(const byte_ring_buffer_t* buffer, uint8_t* out_data, size_t max_length)
{
    size_t i = 0;
    size_t idx = 0;
    if (buffer == 0 || out_data == 0) {
        return 0;
    }

    idx = buffer->tail;
    for (i = 0; i < max_length && i < buffer->size; ++i) {
        out_data[i] = buffer->storage[idx];
        idx = (idx + 1U) % buffer->capacity;
    }
    return i;
}

size_t byte_ring_buffer_size(const byte_ring_buffer_t* buffer)
{
    return (buffer != 0) ? buffer->size : 0;
}

bool byte_ring_buffer_is_empty(const byte_ring_buffer_t* buffer)
{
    return byte_ring_buffer_size(buffer) == 0;
}
