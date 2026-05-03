idf_component_register(
    SRCS "ntrip_client.c"
    INCLUDE_DIRS "."
    REQUIRES log runtime_buffers runtime_components transport_tcp
)
