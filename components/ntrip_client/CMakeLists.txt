idf_component_register(
    SRCS "ntrip_client.c"
    INCLUDE_DIRS "."
    REQUIRES runtime_buffers runtime_components
    PRIV_REQUIRES transport_tcp
)
