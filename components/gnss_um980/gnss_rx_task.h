#pragma once

#include "freertos/FreeRTOS.h"
#include "gnss_um980.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the GNSS RX task for a receiver instance.
 * Creates a FreeRTOS task pinned to Core 0.
 *
 * @param rx     GNSS receiver instance (must be initialized)
 * @param name   Task name (e.g. "gnss_rx1", "gnss_rx2")
 * @return pdPASS on success, errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY on failure
 */
BaseType_t gnss_rx_task_start(gnss_um980_t* rx, const char* name);

#ifdef __cplusplus
}
#endif