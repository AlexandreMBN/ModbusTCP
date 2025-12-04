idf_component_register(
    SRCS "mqtt_client_manager.c" "mqtt_client_manager_ops.c" "mqtt_client_manager_utils.c"
    INCLUDE_DIRS "."
    REQUIRES mqtt esp_wifi esp_netif
)