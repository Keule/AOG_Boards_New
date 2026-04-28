#include "hal_reset.h"

#include "hal_backend.h"
#include "hal_reset_backend.h"

int hal_reset_request(void)
{
    if (hal_backend_get_kind() == HAL_BACKEND_SIM) {
        return hal_reset_backend_sim_request();
    }

    return hal_reset_backend_esp32_request();
}
