idf_component_register(
    SRCS "hal_reset.c" "hal_reset_backend_esp32.c" "hal_reset_backend_sim.c"
    INCLUDE_DIRS "."
    REQUIRES hal_backend
)
