#include "byte_ring_buffer.h"

void byte_ring_buffer_init(byte_ring_buffer_t* buffer, uint8_t* storage, size_t capacity)
{
    if (buffer == NULL) {
        return;
    }

    buffer->data = storage;
    buffer->capacity = capacity;
    buffer->head = 0;
    buffer->tail = 0;
    buffer->size = 0;
    buffer->overflow_count = 0;
}

size_t byte_ring_buffer_write(byte_ring_buffer_t* buffer, const uint8_t* data, size_t length)
{
    size_t written = 0;

    if (buffer == NULL || data == NULL || buffer->data == NULL || buffer->capacity == 0) {
        return 0;
    }

    while (written < length) {
        if (buffer->size >= buffer->capacity) {
            buffer->overflow_count++;
            break;
        }

        buffer->data[buffer->head] = data[written];
        buffer->head = (buffer->head + 1) % buffer->capacity;
        buffer->size++;
        written++;
    }

    return written;
}

size_t byte_ring_buffer_read(byte_ring_buffer_t* buffer, uint8_t* out, size_t length)
{
    size_t read = 0;

    if (buffer == NULL || out == NULL || buffer->data == NULL || buffer->capacity == 0) {
        return 0;
    }

    while (read < length && buffer->size > 0) {
        out[read] = buffer->data[buffer->tail];
        buffer->tail = (buffer->tail + 1) % buffer->capacity;
        buffer->size--;
        read++;
    }

    return read;
}

size_t byte_ring_buffer_available(const byte_ring_buffer_t* buffer)
{
    if (buffer == NULL) {
        return 0;
    }

    return buffer->size;
}

uint32_t byte_ring_buffer_overflow_count(const byte_ring_buffer_t* buffer)
{
    if (buffer == NULL) {
        return 0;
    }

    return buffer->overflow_count;
}
