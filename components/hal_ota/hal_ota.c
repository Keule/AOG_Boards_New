/* ========================================================================
 * hal_ota.c — HAL OTA Abstraction Layer (013_REMOTE_MAINTENANCE-002)
 *
 * ESP-IDF native OTA using esp_ota_ops.h.
 * NOT thread-safe — all calls must be from Core 0 (service loop).
 *
 * Flow:
 *   hal_ota_init()    — set ops table
 *   hal_ota_begin()   — esp_ota_begin() on next partition
 *   hal_ota_write()   — esp_ota_write() in chunks
 *   hal_ota_end()     — esp_ota_end() + esp_ota_set_boot_partition()
 *   hal_ota_reboot()  — esp_restart()
 *
 * State tracking for /ota/status endpoint.
 * ======================================================================== */

#include "hal_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "HAL_OTA";

/* ---- OTA State ---- */

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_IN_PROGRESS,
    OTA_STATE_SUCCESS,
    OTA_STATE_FAILED
} ota_state_t;

static const hal_ota_ops_t* s_ota_ops = NULL;

/* OTA state tracking (accessible via hal_ota_get_state) */
static volatile ota_state_t s_ota_state      = OTA_STATE_IDLE;
static volatile size_t      s_ota_bytes_total = 0;
static volatile size_t      s_ota_bytes_written = 0;
static volatile esp_err_t   s_ota_last_esp_err  = ESP_OK;
static volatile bool        s_reboot_pending     = false;

/* ESP-IDF update handle */
static esp_ota_handle_t s_update_handle = 0;

/* =================================================================
 * Public API — dispatch through ops table (or direct if ESP32)
 * ================================================================= */

hal_err_t hal_ota_init(const hal_ota_ops_t* ops)
{
    if (ops == NULL) return HAL_ERR_INVALID_PARAM;
    s_ota_ops = ops;
    ESP_LOGI(TAG, "HAL OTA initialized");
    return HAL_OK;
}

hal_err_t hal_ota_begin(size_t image_size)
{
    if (s_ota_ops == NULL || s_ota_ops->begin == NULL) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    s_ota_state = OTA_STATE_IN_PROGRESS;
    s_ota_bytes_total = image_size;
    s_ota_bytes_written = 0;
    s_ota_last_esp_err = ESP_OK;
    return s_ota_ops->begin(image_size);
}

hal_err_t hal_ota_write(const uint8_t* data, size_t len)
{
    if (s_ota_ops == NULL || s_ota_ops->write == NULL) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    return s_ota_ops->write(data, len);
}

hal_err_t hal_ota_end(void)
{
    if (s_ota_ops == NULL || s_ota_ops->end == NULL) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    hal_err_t err = s_ota_ops->end();
    if (err == HAL_OK) {
        s_ota_state = OTA_STATE_SUCCESS;
        s_reboot_pending = true;
    } else {
        s_ota_state = OTA_STATE_FAILED;
    }
    return err;
}

hal_err_t hal_ota_reboot(void)
{
    if (s_ota_ops == NULL || s_ota_ops->reboot == NULL) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    return s_ota_ops->reboot();
}

/* =================================================================
 * State queries (for /ota/status endpoint)
 * ================================================================= */

bool hal_ota_is_in_progress(void)
{
    return (s_ota_state == OTA_STATE_IN_PROGRESS);
}

bool hal_ota_is_reboot_pending(void)
{
    return s_reboot_pending;
}

size_t hal_ota_get_bytes_total(void)
{
    return s_ota_bytes_total;
}

size_t hal_ota_get_bytes_written(void)
{
    return s_ota_bytes_written;
}

const char* hal_ota_get_state_str(void)
{
    switch (s_ota_state) {
        case OTA_STATE_IDLE:        return "idle";
        case OTA_STATE_IN_PROGRESS: return "in_progress";
        case OTA_STATE_SUCCESS:     return "success";
        case OTA_STATE_FAILED:      return "failed";
        default:                    return "unknown";
    }
}

esp_err_t hal_ota_get_last_error(void)
{
    return s_ota_last_esp_err;
}

const char* hal_ota_get_active_partition_label(void)
{
    const esp_partition_t* p = esp_ota_get_running_partition();
    return (p != NULL) ? p->label : "unknown";
}

const char* hal_ota_get_next_partition_label(void)
{
    const esp_partition_t* p = esp_ota_get_next_update_partition(NULL);
    return (p != NULL) ? p->label : "none";
}

bool hal_ota_is_supported(void)
{
    /* OTA is always supported with this partition layout */
    return true;
}

/* =================================================================
 * ESP32 OTA Ops — real implementation using esp_ota_ops.h
 * ================================================================= */

static hal_err_t esp32_ota_begin(size_t image_size)
{
    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "No OTA update partition available");
        s_ota_last_esp_err = ESP_ERR_NOT_FOUND;
        s_ota_state = OTA_STATE_FAILED;
        return HAL_ERR_NOT_SUPPORTED;
    }

    ESP_LOGI(TAG, "OTA begin: partition=%s size=%u", update_partition->label, (unsigned)image_size);

    esp_err_t err = esp_ota_begin(update_partition, image_size, &s_update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: 0x%x (%s)", (unsigned)err, esp_err_to_name(err));
        s_ota_last_esp_err = err;
        s_ota_state = OTA_STATE_FAILED;
        return HAL_ERR_IO;
    }

    return HAL_OK;
}

static hal_err_t esp32_ota_write(const uint8_t* data, size_t len)
{
    esp_err_t err = esp_ota_write(s_update_handle, (const void*)data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed: 0x%x (%s)", (unsigned)err, esp_err_to_name(err));
        s_ota_last_esp_err = err;
        s_ota_state = OTA_STATE_FAILED;
        return HAL_ERR_IO;
    }
    s_ota_bytes_written += len;
    return HAL_OK;
}

static hal_err_t esp32_ota_end(void)
{
    esp_err_t err = esp_ota_end(s_update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: 0x%x (%s)", (unsigned)err, esp_err_to_name(err));
        s_ota_last_esp_err = err;
        return HAL_ERR_IO;
    }

    /* Set boot partition */
    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "Cannot get update partition for boot");
        s_ota_last_esp_err = ESP_ERR_NOT_FOUND;
        return HAL_ERR_IO;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: 0x%x (%s)",
                 (unsigned)err, esp_err_to_name(err));
        s_ota_last_esp_err = err;
        return HAL_ERR_IO;
    }

    ESP_LOGI(TAG, "OTA end: %u bytes written, boot partition set to %s",
             (unsigned)s_ota_bytes_written, update_partition->label);
    s_update_handle = 0;
    return HAL_OK;
}

static hal_err_t esp32_ota_reboot(void)
{
    ESP_LOGW(TAG, "OTA reboot requested");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    /* Never returns */
    return HAL_OK;
}

static const hal_ota_ops_t s_esp32_ota_ops = {
    .begin  = esp32_ota_begin,
    .write  = esp32_ota_write,
    .end    = esp32_ota_end,
    .reboot = esp32_ota_reboot,
};

const hal_ota_ops_t* hal_ota_esp32_ops(void)
{
    return &s_esp32_ota_ops;
}
