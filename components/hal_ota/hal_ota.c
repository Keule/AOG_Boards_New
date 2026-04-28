#include "hal_ota.h"

#include "hal_backend.h"
#include "hal_ota_backend.h"

int hal_ota_begin(size_t image_size)
{
    if (hal_backend_get_kind() == HAL_BACKEND_SIM) {
        return hal_ota_backend_sim_begin(image_size);
    }

    return hal_ota_backend_esp32_begin(image_size);
}

int hal_ota_write(const uint8_t* data, size_t size)
{
    if (hal_backend_get_kind() == HAL_BACKEND_SIM) {
        return hal_ota_backend_sim_write(data, size);
    }

    return hal_ota_backend_esp32_write(data, size);
}

int hal_ota_end(void)
{
    if (hal_backend_get_kind() == HAL_BACKEND_SIM) {
        return hal_ota_backend_sim_end();
    }

    return hal_ota_backend_esp32_end();
}

int hal_ota_reboot(void)
{
    if (hal_backend_get_kind() == HAL_BACKEND_SIM) {
        return hal_ota_backend_sim_reboot();
    }

    return hal_ota_backend_esp32_reboot();
}
