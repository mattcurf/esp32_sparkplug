idf_component_register(
    SRCS "app_console.c"
    INCLUDE_DIRS "include"
    REQUIRES console esp_timer
    PRIV_REQUIRES esp_app_format esp_system
)
