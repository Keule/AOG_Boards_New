idf_component_register(
    SRCS "aog_steering_app.c"
    INCLUDE_DIRS "."
    REQUIRES runtime_components runtime_buffers runtime_snapshot protocol_aog
)
