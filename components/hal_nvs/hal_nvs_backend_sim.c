#include "hal_nvs_backend.h"

int hal_nvs_backend_sim_init(void)
{
    return 0;
}

int hal_nvs_backend_sim_set_blob(const char* key, const void* data, size_t size)
{
    (void)key;
    (void)data;
    (void)size;
    return -1;
}

int hal_nvs_backend_sim_get_blob(const char* key, void* out_data, size_t* inout_size)
{
    (void)key;
    (void)out_data;
    (void)inout_size;
    return -1;
}
