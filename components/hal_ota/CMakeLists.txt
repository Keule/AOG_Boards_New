idf_component_register(
    SRCS "hal_ota.c" "hal_ota_backend_esp32.c" "hal_ota_backend_sim.c"
    INCLUDE_DIRS "."
    REQUIRES hal_backend
)
