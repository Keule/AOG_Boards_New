#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t lines_written;
    uint32_t lines_dropped;
    uint32_t bytes_written;
    uint32_t bytes_overwritten;
    size_t buffer_size;
    size_t used_bytes;
} remote_log_stats_t;

void remote_log_init(void);
bool remote_log_is_initialized(void);

size_t remote_log_read(char* out, size_t out_size);
size_t remote_log_read_tail_lines(char* out, size_t out_size, uint32_t max_lines);

void remote_log_clear(void);
void remote_log_get_stats(remote_log_stats_t* out);

#ifdef __cplusplus
}
#endif
