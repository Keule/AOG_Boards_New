#include "snapshot_buffer.h"

#include <string.h>

void snapshot_buffer_init(snapshot_buffer_t* buffer, void* storage, size_t value_size)
{
    if (buffer == NULL) {
        return;
    }

    buffer->storage = (uint8_t*)storage;
    buffer->value_size = value_size;
    buffer->valid = false;
    buffer->sequence = 0;
}

void snapshot_buffer_set(snapshot_buffer_t* buffer, const void* value)
{
    if (buffer == NULL || value == NULL || buffer->storage == NULL || buffer->value_size == 0) {
        return;
    }

    memcpy(buffer->storage, value, buffer->value_size);
    buffer->valid = true;
    buffer->sequence++;
}

bool snapshot_buffer_get(const snapshot_buffer_t* buffer, void* out_value)
{
    if (buffer == NULL || out_value == NULL || buffer->storage == NULL || buffer->value_size == 0 || !buffer->valid) {
        return false;
    }

    memcpy(out_value, buffer->storage, buffer->value_size);

    return true;
}

bool snapshot_buffer_is_valid(const snapshot_buffer_t* buffer)
{
    if (buffer == NULL) {
        return false;
    }

    return buffer->valid;
}

uint32_t snapshot_buffer_sequence(const snapshot_buffer_t* buffer)
{
    if (buffer == NULL) {
        return 0;
    }

    return buffer->sequence;
}
