#include "hal_ota_backend.h"

int hal_ota_backend_esp32_begin(size_t image_size)
{
    (void)image_size;
    return 0;
}

int hal_ota_backend_esp32_write(const uint8_t* data, size_t size)
{
    (void)data;
    (void)size;
    return 0;
}

int hal_ota_backend_esp32_end(void)
{
    return 0;
}

int hal_ota_backend_esp32_reboot(void)
{
    return 0;
}
