idf_component_register(
    SRCS "runtime.c" "runtime_mode.c"
    INCLUDE_DIRS "."
    REQUIRES runtime_fast runtime_components esp_timer log
)
