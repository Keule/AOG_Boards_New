#include "hal_backend.h"

hal_backend_kind_t hal_backend_get_kind(void)
{
#if defined(HAL_BACKEND_SIM)
    return HAL_BACKEND_SIM;
#else
    return HAL_BACKEND_ESP32;
#endif
}
