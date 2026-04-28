#include "hal_time_backend.h"

uint64_t hal_time_backend_sim_us(void)
{
    static uint64_t s_sim_time_us = 0;
    s_sim_time_us += 1000ULL;
    return s_sim_time_us;
}
