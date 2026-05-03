idf_component_register(
    SRCS "transport_tcp.c"
    INCLUDE_DIRS "."
    REQUIRES hal_backend runtime_buffers runtime_components lwip esp_timer log
)
