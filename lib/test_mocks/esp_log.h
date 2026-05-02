#pragma once
/*
 * Stub esp_log.h for native tests.
 * Replaces ESP-IDF's logging macros with no-ops.
 */
#include <stdio.h>

#define ESP_LOG_NONE       0
#define ESP_LOG_ERROR      1
#define ESP_LOG_WARN       2
#define ESP_LOG_INFO       3
#define ESP_LOG_DEBUG      4
#define ESP_LOG_VERBOSE    5

#define LOG_LOCAL_LEVEL    ESP_LOG_VERBOSE

// No-op log macros for native testing
#define ESP_LOG_LEVEL(level, tag, format, ...) \
    do { if (level <= ESP_LOG_INFO) printf("[%s] " format "\n", tag, ##__VA_ARGS__); } while(0)

#define ESP_LOGE(tag, format, ...)   ESP_LOG_LEVEL(ESP_LOG_ERROR,   tag, format, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...)   ESP_LOG_LEVEL(ESP_LOG_WARN,    tag, format, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...)   ESP_LOG_LEVEL(ESP_LOG_INFO,    tag, format, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...)   ESP_LOG_LEVEL(ESP_LOG_DEBUG,   tag, format, ##__VA_ARGS__)
#define ESP_LOGV(tag, format, ...)   ESP_LOG_LEVEL(ESP_LOG_VERBOSE, tag, format, ##__VA_ARGS__)
