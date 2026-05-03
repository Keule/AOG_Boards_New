idf_component_register(
    SRCS "hal_eth.c"
    INCLUDE_DIRS "."
    REQUIRES hal_backend board_profiles esp_eth esp_netif esp_event esp_driver_gpio
)
