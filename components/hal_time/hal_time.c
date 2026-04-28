#include "hal_time.h"

#include "hal_backend.h"
#include "hal_time_backend.h"

uint64_t hal_time_us(void)
{
    if (hal_backend_get_kind() == HAL_BACKEND_SIM) {
        return hal_time_backend_sim_us();
    }

    return hal_time_backend_esp32_us();
}

uint32_t hal_time_ms(void)
{
    return (uint32_t)(hal_time_us() / 1000ULL);
}
