#include "message_queue.h"

#include <string.h>

void message_queue_init(message_queue_t* queue, void* storage, size_t element_size, size_t capacity)
{
    if (queue == NULL) {
        return;
    }

    queue->storage = (uint8_t*)storage;
    queue->element_size = element_size;
    queue->capacity = capacity;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
}

bool message_queue_push(message_queue_t* queue, const void* element)
{
    uint8_t* destination = NULL;

    if (queue == NULL || element == NULL || queue->storage == NULL || queue->element_size == 0 || queue->capacity == 0) {
        return false;
    }

    if (queue->count >= queue->capacity) {
        return false;
    }

    destination = queue->storage + (queue->head * queue->element_size);
    memcpy(destination, element, queue->element_size);

    queue->head = (queue->head + 1) % queue->capacity;
    queue->count++;

    return true;
}

bool message_queue_pop(message_queue_t* queue, void* out_element)
{
    uint8_t* source = NULL;

    if (queue == NULL || out_element == NULL || queue->storage == NULL || queue->element_size == 0 || queue->capacity == 0) {
        return false;
    }

    if (queue->count == 0) {
        return false;
    }

    source = queue->storage + (queue->tail * queue->element_size);
    memcpy(out_element, source, queue->element_size);

    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count--;

    return true;
}

size_t message_queue_count(const message_queue_t* queue)
{
    if (queue == NULL) {
        return 0;
    }

    return queue->count;
}
