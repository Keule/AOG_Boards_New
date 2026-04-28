#include "message_queue.h"

#include <string.h>

void message_queue_init(message_queue_t* queue, void* storage, size_t item_size, size_t capacity)
{
    if (queue == 0 || storage == 0 || item_size == 0 || capacity == 0) {
        return;
    }

    queue->storage = storage;
    queue->item_size = item_size;
    queue->capacity = capacity;
    queue->head = 0;
    queue->tail = 0;
    queue->size = 0;
}

bool message_queue_push(message_queue_t* queue, const void* item)
{
    uint8_t* base = 0;
    if (queue == 0 || item == 0 || queue->size >= queue->capacity) {
        return false;
    }

    base = (uint8_t*)queue->storage;
    memcpy(&base[queue->head * queue->item_size], item, queue->item_size);
    queue->head = (queue->head + 1U) % queue->capacity;
    queue->size++;
    return true;
}

bool message_queue_pop(message_queue_t* queue, void* out_item)
{
    uint8_t* base = 0;
    if (queue == 0 || out_item == 0 || queue->size == 0) {
        return false;
    }

    base = (uint8_t*)queue->storage;
    memcpy(out_item, &base[queue->tail * queue->item_size], queue->item_size);
    queue->tail = (queue->tail + 1U) % queue->capacity;
    queue->size--;
    return true;
}

size_t message_queue_size(const message_queue_t* queue)
{
    return (queue != 0) ? queue->size : 0;
}
