idf_component_register(
    SRCS "hal_time.c" "hal_time_backend_esp32.c" "hal_time_backend_sim.c"
    INCLUDE_DIRS "."
    REQUIRES hal_backend esp_timer
)
