idf_component_register(
    SRCS "transport_udp.c"
    INCLUDE_DIRS "."
    REQUIRES hal_backend runtime_buffers runtime_components
)
