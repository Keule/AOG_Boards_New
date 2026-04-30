#include "runtime.h"
#include "runtime_mode.h"
#include "runtime_component.h"
#include "runtime_types.h"
#include "task_fast.h"

#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "RUNTIME";

/* ---- Task argument: holds pointer to group for live profile lookup ---- */

typedef struct {
    service_group_t group;
} service_task_arg_t;

/* ---- Unified service task function ----
 *
 * Reads the live profile from runtime_mode_get_profile() every loop
 * iteration.  This ensures profile changes via runtime_set_system_mode()
 * are visible without task restart.                                 */

static void service_task_fn(void* arg)
{
    service_task_arg_t* sa = (service_task_arg_t*)arg;
    service_group_t group = sa->group;

    ESP_LOGI(TAG, "%s_service_task started",
             (group == SERVICE_GROUP_UART)      ? "uart" :
             (group == SERVICE_GROUP_UDP)       ? "udp" :
             (group == SERVICE_GROUP_TCP_NTRIP) ? "tcp_ntrip" :
                                                  "diagnostics");

    for (;;) {
        /* Read live profile from central state — NOT a cached copy. */
        const service_profile_t* prof = runtime_mode_get_profile(group);

        if (prof != NULL && !prof->suspended) {
            int64_t now = esp_timer_get_time();
            runtime_service_step_group(group, (uint64_t)now);
        }

        vTaskDelay(pdMS_TO_TICKS(prof ? prof->period_ms : 10));
    }
}

/* ---- Static task argument storage (must outlive tasks) ---- */

static service_task_arg_t s_uart_arg      = { .group = SERVICE_GROUP_UART };
static service_task_arg_t s_udp_arg       = { .group = SERVICE_GROUP_UDP };
static service_task_arg_t s_tcp_ntrip_arg = { .group = SERVICE_GROUP_TCP_NTRIP };
static service_task_arg_t s_diag_arg      = { .group = SERVICE_GROUP_DIAGNOSTICS };

/* ---- Stored task handles for vTaskPrioritySet() (TODO) ---- */
/* static TaskHandle_t s_task_handles[SERVICE_GROUP_COUNT] = {0}; */

/* =========================================================================
 * Public API
 * ========================================================================= */

void runtime_init(void)
{
    runtime_mode_init();
    ESP_LOGI(TAG, "runtime_init: mode=WORK (default)");
}

void runtime_start(void)
{
    /* Start task_fast on Core 1 (deterministic, mode-free) */
    task_fast_start();

    /* --- Core 0 service tasks --- */

    xTaskCreatePinnedToCore(
        service_task_fn, "uart_svc", 4096, &s_uart_arg,
        runtime_mode_get_profile(SERVICE_GROUP_UART)->priority,
        NULL, 0
    );

    xTaskCreatePinnedToCore(
        service_task_fn, "udp_svc", 4096, &s_udp_arg,
        runtime_mode_get_profile(SERVICE_GROUP_UDP)->priority,
        NULL, 0
    );

    xTaskCreatePinnedToCore(
        service_task_fn, "tcp_ntrip_svc", 4096, &s_tcp_ntrip_arg,
        runtime_mode_get_profile(SERVICE_GROUP_TCP_NTRIP)->priority,
        NULL, 0
    );

    xTaskCreatePinnedToCore(
        service_task_fn, "diag_svc", 4096, &s_diag_arg,
        runtime_mode_get_profile(SERVICE_GROUP_DIAGNOSTICS)->priority,
        NULL, 0
    );

    ESP_LOGI(TAG, "Core-0 service tasks started: 4 groups");
}

const service_profile_t* runtime_get_service_profile(service_group_t group)
{
    return runtime_mode_get_profile(group);
}

int runtime_set_system_mode(system_mode_t mode)
{
    int ret = runtime_mode_set(mode);
    if (ret != 0) {
        ESP_LOGW(TAG, "runtime_set_system_mode: invalid mode=%d, rejected", (int)mode);
        return -1;
    }

    ESP_LOGI(TAG, "runtime_set_system_mode: mode=%s",
             (mode == SYSTEM_MODE_WORK) ? "WORK" : "CONFIG");

    /* TODO: vTaskPrioritySet() for live priority changes.
     * Store task handles at creation time and call
     * vTaskPrioritySet(handle, new_priority) here.
     * Period changes already take effect on next loop iteration. */

    return 0;
}

system_mode_t runtime_get_system_mode(void)
{
    return runtime_mode_get();
}
