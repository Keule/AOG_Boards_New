idf_component_register(
    SRCS "hal_eth.c" "hal_eth_backend_esp32.c" "hal_eth_backend_sim.c"
    INCLUDE_DIRS "."
    REQUIRES hal_backend board_profiles
)
