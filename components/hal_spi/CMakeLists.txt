idf_component_register(
    SRCS "hal_spi.c" "hal_spi_backend_esp32.c" "hal_spi_backend_sim.c"
    INCLUDE_DIRS "."
    REQUIRES hal_backend
)
