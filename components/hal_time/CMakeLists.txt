idf_component_register(
    SRCS "hal_time.c"
    INCLUDE_DIRS "."
    REQUIRES hal_backend esp_timer
)
