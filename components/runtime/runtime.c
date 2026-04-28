#include "runtime.h"

#include "runtime_component.h"
#include "task_fast.h"
#include "task_service.h"

/* ---- Public API ---- */

void runtime_init(void)
{
    runtime_component_clear_all();
}

void runtime_start(void)
{
    task_fast_start();
    task_service_start();
}
