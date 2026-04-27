#include "runtime.h"
#include "task_fast.h"

void runtime_init(void)
{
}

void runtime_start(void)
{
    task_fast_start();
}
