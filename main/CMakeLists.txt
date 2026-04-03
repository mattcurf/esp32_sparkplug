idf_component_register(
    SRCS "app_main.c"
    REQUIRES app_config app_console sensor_tmp36 sparkplug_node time_sync wifi_manager
    PRIV_REQUIRES esp_event esp_netif freertos nvs_flash
)
