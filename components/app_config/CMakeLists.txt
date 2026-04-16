idf_component_register(
    SRCS "app_config.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_adc esp_driver_gpio nvs_flash
)
