idf_component_register(
    SRCS "src/ap_manager.c"
         "src/ap_manager_config.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif esp_event esp_system nvs_flash spiffs json
)