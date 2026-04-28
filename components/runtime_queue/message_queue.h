#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t* storage;
    size_t element_size;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
} message_queue_t;

void message_queue_init(message_queue_t* queue, void* storage, size_t element_size, size_t capacity);
bool message_queue_push(message_queue_t* queue, const void* element);
bool message_queue_pop(message_queue_t* queue, void* out_element);
size_t message_queue_count(const message_queue_t* queue);

#ifdef __cplusplus
}
#endif
