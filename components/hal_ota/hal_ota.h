#pragma once

/* ========================================================================
 * hal_ota.h — HAL OTA Abstraction Layer (013_REMOTE_MAINTENANCE-002)
 *
 * ESP-IDF native OTA using esp_ota_ops.h.
 * NOT thread-safe — all calls must be from Core 0 (service loop).
 * ======================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hal_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- HAL OTA Ops Table ---- */

typedef struct {
    hal_err_t (*begin)(size_t image_size);
    hal_err_t (*write)(const uint8_t* data, size_t len);
    hal_err_t (*end)(void);
    hal_err_t (*reboot)(void);
} hal_ota_ops_t;

/* ---- HAL OTA API ---- */

/* Initialize with ops table. Pass hal_ota_esp32_ops() for ESP32. */
hal_err_t hal_ota_init(const hal_ota_ops_t* ops);

/* Start OTA update. Determines next partition, validates size. */
hal_err_t hal_ota_begin(size_t image_size);

/* Write chunk of firmware data. Call after begin(), before end(). */
hal_err_t hal_ota_write(const uint8_t* data, size_t len);

/* Finalize OTA: validate image, set boot partition. */
hal_err_t hal_ota_end(void);

/* Reboot device. Typically called automatically after successful OTA. */
hal_err_t hal_ota_reboot(void);

/* ---- State Queries (for /ota/status endpoint) ---- */

bool    hal_ota_is_in_progress(void);
bool    hal_ota_is_reboot_pending(void);
bool    hal_ota_is_supported(void);
size_t  hal_ota_get_bytes_total(void);
size_t  hal_ota_get_bytes_written(void);
const char* hal_ota_get_state_str(void);
esp_err_t hal_ota_get_last_error(void);
const char* hal_ota_get_active_partition_label(void);
const char* hal_ota_get_next_partition_label(void);

/* ---- ESP32 ops factory ---- */
const hal_ota_ops_t* hal_ota_esp32_ops(void);

#ifdef __cplusplus
}
#endif
