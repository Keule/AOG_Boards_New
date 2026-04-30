idf_component_register(
    SRCS "app_main.cpp"
    INCLUDE_DIRS "."
    REQUIRES app_core
)
set(EXCLUDE_COMPONENTS esp_lcd) 