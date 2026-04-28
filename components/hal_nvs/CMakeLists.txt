idf_component_register(
    SRCS "hal_nvs.c" "hal_nvs_backend_esp32.c" "hal_nvs_backend_sim.c"
    INCLUDE_DIRS "."
    REQUIRES hal_backend
)
