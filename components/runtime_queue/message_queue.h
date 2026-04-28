#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void* storage;
    size_t item_size;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t size;
} message_queue_t;

void message_queue_init(message_queue_t* queue, void* storage, size_t item_size, size_t capacity);
bool message_queue_push(message_queue_t* queue, const void* item);
bool message_queue_pop(message_queue_t* queue, void* out_item);
size_t message_queue_size(const message_queue_t* queue);

#ifdef __cplusplus
}
#endif
