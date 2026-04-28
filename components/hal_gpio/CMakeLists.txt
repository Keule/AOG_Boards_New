idf_component_register(
    SRCS "hal_gpio.c" "hal_gpio_backend_esp32.c" "hal_gpio_backend_sim.c"
    INCLUDE_DIRS "."
    REQUIRES hal_backend
)
