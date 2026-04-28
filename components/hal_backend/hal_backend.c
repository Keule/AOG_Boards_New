#include "hal_backend.h"

static hal_backend_type_t s_backend = HAL_BACKEND_NONE;

void hal_backend_set(hal_backend_type_t backend)
{
    s_backend = backend;
}

hal_backend_type_t hal_backend_get(void)
{
    return s_backend;
}
