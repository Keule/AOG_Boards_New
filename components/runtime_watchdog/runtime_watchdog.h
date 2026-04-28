#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void runtime_watchdog_init(void);
void runtime_watchdog_register_task(const char* task_name);
void runtime_watchdog_feed(const char* task_name);

#ifdef __cplusplus
}
#endif
