idf_component_register(
    SRCS "task_fast.c"
    INCLUDE_DIRS "."
    REQUIRES runtime_types runtime_components runtime_stats runtime_watchdog
)
