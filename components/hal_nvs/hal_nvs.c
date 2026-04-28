#include "hal_nvs.h"

#include "hal_backend.h"
#include "hal_nvs_backend.h"

int hal_nvs_init(void)
{
    if (hal_backend_get_kind() == HAL_BACKEND_SIM) {
        return hal_nvs_backend_sim_init();
    }

    return hal_nvs_backend_esp32_init();
}

int hal_nvs_set_blob(const char* key, const void* data, size_t size)
{
    if (hal_backend_get_kind() == HAL_BACKEND_SIM) {
        return hal_nvs_backend_sim_set_blob(key, data, size);
    }

    return hal_nvs_backend_esp32_set_blob(key, data, size);
}

int hal_nvs_get_blob(const char* key, void* out_data, size_t* inout_size)
{
    if (hal_backend_get_kind() == HAL_BACKEND_SIM) {
        return hal_nvs_backend_sim_get_blob(key, out_data, inout_size);
    }

    return hal_nvs_backend_esp32_get_blob(key, out_data, inout_size);
}
