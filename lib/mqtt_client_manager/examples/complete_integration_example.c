/**
 * @file complete_integration_example.c
 * @brief Complete MQTT Client Manager Integration Example
 * 
 * This example demonstrates full integration of MQTT Client Manager with all other libraries:
 * - WiFi Manager for network connectivity
 * - Config Manager for configuration storage and management
 * - SPIFFS File Manager for certificate and data storage
 * - AP Manager for configuration portal
 * - Dynamic configuration through web interface
 * - Real-world sensor data publishing
 * 
 * @version 1.0.0
 * @date 2024-11-10
 */

#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <cJSON.h>

/* Include all library managers */
#include "mqtt_client_manager.h"
#include "wifi_manager.h"
#include "config_manager.h"
#include "spiffs_file_manager.h"
#include "ap_manager.h"

#define TAG "MQTT_COMPLETE_EXAMPLE"

/* Configuration keys for Config Manager integration */
#define CONFIG_KEY_MQTT_ENABLED       "mqtt_enabled"
#define CONFIG_KEY_MQTT_BROKER_URL    "mqtt_broker_url"
#define CONFIG_KEY_MQTT_PORT          "mqtt_port"
#define CONFIG_KEY_MQTT_CLIENT_ID     "mqtt_client_id"
#define CONFIG_KEY_MQTT_USERNAME      "mqtt_username"
#define CONFIG_KEY_MQTT_PASSWORD      "mqtt_password"
#define CONFIG_KEY_MQTT_SSL_ENABLED   "mqtt_ssl_enabled"
#define CONFIG_KEY_MQTT_QOS           "mqtt_default_qos"
#define CONFIG_KEY_PUBLISH_INTERVAL   "mqtt_publish_interval"

/* Application defaults */
#define DEFAULT_MQTT_BROKER_URL       "mqtt://broker.hivemq.com"
#define DEFAULT_MQTT_PORT             1883
#define DEFAULT_MQTT_CLIENT_ID        "esp32_complete_example"
#define DEFAULT_PUBLISH_INTERVAL_MS   30000

/* Event group bits */
#define SYSTEM_READY_BIT              BIT0
#define WIFI_CONNECTED_BIT            BIT1
#define MQTT_CONNECTED_BIT            BIT2
#define CONFIG_LOADED_BIT             BIT3

/* Global variables */
static EventGroupHandle_t g_system_events = NULL;
static bool g_mqtt_enabled = true;
static uint32_t g_publish_interval_ms = DEFAULT_PUBLISH_INTERVAL_MS;
static uint32_t g_sensor_reading_count = 0;

/* Configuration structure */
typedef struct {
    bool mqtt_enabled;
    char mqtt_broker_url[256];
    uint16_t mqtt_port;
    char mqtt_client_id[64];
    char mqtt_username[64];
    char mqtt_password[64];
    bool mqtt_ssl_enabled;
    int mqtt_default_qos;
    uint32_t publish_interval_ms;
} app_config_t;

static app_config_t g_app_config;

/* ============================= CONFIGURATION MANAGEMENT ============================= */

/**
 * @brief Load application configuration from Config Manager
 */
static esp_err_t load_application_config(void)
{
    ESP_LOGI(TAG, "Loading application configuration...");
    
    /* Set defaults first */
    g_app_config.mqtt_enabled = true;
    strncpy(g_app_config.mqtt_broker_url, DEFAULT_MQTT_BROKER_URL, sizeof(g_app_config.mqtt_broker_url) - 1);
    g_app_config.mqtt_port = DEFAULT_MQTT_PORT;
    strncpy(g_app_config.mqtt_client_id, DEFAULT_MQTT_CLIENT_ID, sizeof(g_app_config.mqtt_client_id) - 1);
    g_app_config.mqtt_ssl_enabled = false;
    g_app_config.mqtt_default_qos = 1;
    g_app_config.publish_interval_ms = DEFAULT_PUBLISH_INTERVAL_MS;
    
    /* Load from Config Manager */
    bool bool_value;
    if (config_manager_get_bool(CONFIG_KEY_MQTT_ENABLED, &bool_value) == ESP_OK) {
        g_app_config.mqtt_enabled = bool_value;
    }
    
    char string_value[256];
    if (config_manager_get_string(CONFIG_KEY_MQTT_BROKER_URL, string_value, sizeof(string_value)) == ESP_OK) {
        strncpy(g_app_config.mqtt_broker_url, string_value, sizeof(g_app_config.mqtt_broker_url) - 1);
    }
    
    int int_value;
    if (config_manager_get_int(CONFIG_KEY_MQTT_PORT, &int_value) == ESP_OK) {
        g_app_config.mqtt_port = (uint16_t)int_value;
    }
    
    if (config_manager_get_string(CONFIG_KEY_MQTT_CLIENT_ID, string_value, sizeof(string_value)) == ESP_OK) {
        strncpy(g_app_config.mqtt_client_id, string_value, sizeof(g_app_config.mqtt_client_id) - 1);
    }
    
    if (config_manager_get_string(CONFIG_KEY_MQTT_USERNAME, string_value, sizeof(string_value)) == ESP_OK) {
        strncpy(g_app_config.mqtt_username, string_value, sizeof(g_app_config.mqtt_username) - 1);
    }
    
    if (config_manager_get_string(CONFIG_KEY_MQTT_PASSWORD, string_value, sizeof(string_value)) == ESP_OK) {
        strncpy(g_app_config.mqtt_password, string_value, sizeof(g_app_config.mqtt_password) - 1);
    }
    
    if (config_manager_get_bool(CONFIG_KEY_MQTT_SSL_ENABLED, &bool_value) == ESP_OK) {
        g_app_config.mqtt_ssl_enabled = bool_value;
    }
    
    if (config_manager_get_int(CONFIG_KEY_MQTT_QOS, &int_value) == ESP_OK) {
        g_app_config.mqtt_default_qos = int_value;
    }
    
    if (config_manager_get_int(CONFIG_KEY_PUBLISH_INTERVAL, &int_value) == ESP_OK) {
        g_app_config.publish_interval_ms = (uint32_t)int_value;
    }
    
    /* Update global variables */
    g_mqtt_enabled = g_app_config.mqtt_enabled;
    g_publish_interval_ms = g_app_config.publish_interval_ms;
    
    ESP_LOGI(TAG, "Configuration loaded:");
    ESP_LOGI(TAG, "  MQTT Enabled: %s", g_app_config.mqtt_enabled ? "Yes" : "No");
    ESP_LOGI(TAG, "  Broker: %s:%d", g_app_config.mqtt_broker_url, g_app_config.mqtt_port);
    ESP_LOGI(TAG, "  Client ID: %s", g_app_config.mqtt_client_id);
    ESP_LOGI(TAG, "  SSL: %s", g_app_config.mqtt_ssl_enabled ? "Enabled" : "Disabled");
    ESP_LOGI(TAG, "  Publish Interval: %d ms", g_app_config.publish_interval_ms);
    
    xEventGroupSetBits(g_system_events, CONFIG_LOADED_BIT);
    return ESP_OK;
}

/**
 * @brief Save current configuration to Config Manager
 */
static esp_err_t save_application_config(void)
{
    ESP_LOGI(TAG, "Saving application configuration...");
    
    esp_err_t ret = ESP_OK;
    
    ret |= config_manager_set_bool(CONFIG_KEY_MQTT_ENABLED, g_app_config.mqtt_enabled);
    ret |= config_manager_set_string(CONFIG_KEY_MQTT_BROKER_URL, g_app_config.mqtt_broker_url);
    ret |= config_manager_set_int(CONFIG_KEY_MQTT_PORT, g_app_config.mqtt_port);
    ret |= config_manager_set_string(CONFIG_KEY_MQTT_CLIENT_ID, g_app_config.mqtt_client_id);
    ret |= config_manager_set_string(CONFIG_KEY_MQTT_USERNAME, g_app_config.mqtt_username);
    ret |= config_manager_set_string(CONFIG_KEY_MQTT_PASSWORD, g_app_config.mqtt_password);
    ret |= config_manager_set_bool(CONFIG_KEY_MQTT_SSL_ENABLED, g_app_config.mqtt_ssl_enabled);
    ret |= config_manager_set_int(CONFIG_KEY_MQTT_QOS, g_app_config.mqtt_default_qos);
    ret |= config_manager_set_int(CONFIG_KEY_PUBLISH_INTERVAL, g_app_config.publish_interval_ms);
    
    if (ret == ESP_OK) {
        ret = config_manager_commit();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Configuration saved successfully");
        } else {
            ESP_LOGE(TAG, "Failed to commit configuration: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGE(TAG, "Failed to set configuration values");
    }
    
    return ret;
}

/* ============================= MQTT INTEGRATION ============================= */

/**
 * @brief MQTT event handler with full integration
 */
static void mqtt_integration_event_handler(mqtt_client_manager_event_t event, void* data, void* user_data)
{
    switch (event) {
        case MQTT_CLIENT_MANAGER_EVENT_BEFORE_CONNECT:
            ESP_LOGI(TAG, "Connecting to MQTT broker: %s:%d", 
                     g_app_config.mqtt_broker_url, g_app_config.mqtt_port);
            break;
            
        case MQTT_CLIENT_MANAGER_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected to MQTT broker!");
            xEventGroupSetBits(g_system_events, MQTT_CONNECTED_BIT);
            
            /* Subscribe to device management topics */
            mqtt_client_manager_subscribe("device/+/command", MQTT_CLIENT_MANAGER_QOS_1);
            mqtt_client_manager_subscribe("system/config/update", MQTT_CLIENT_MANAGER_QOS_2);
            mqtt_client_manager_subscribe("system/restart", MQTT_CLIENT_MANAGER_QOS_2);
            
            /* Publish device online status */
            char online_payload[256];
            snprintf(online_payload, sizeof(online_payload),
                    "{"
                    "\"device_id\":\"%s\","
                    "\"status\":\"online\","
                    "\"timestamp\":%lld,"
                    "\"version\":\"1.0.0\","
                    "\"capabilities\":[\"sensors\",\"remote_config\",\"ssl\"]"
                    "}",
                    g_app_config.mqtt_client_id,
                    esp_timer_get_time() / 1000);
            
            mqtt_client_manager_publish_json("device/status", online_payload,
                                            MQTT_CLIENT_MANAGER_QOS_1, true);
            break;
            
        case MQTT_CLIENT_MANAGER_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Disconnected from MQTT broker");
            xEventGroupClearBits(g_system_events, MQTT_CONNECTED_BIT);
            break;
            
        case MQTT_CLIENT_MANAGER_EVENT_CONNECTION_LOST:
            ESP_LOGW(TAG, "MQTT connection lost, will auto-reconnect");
            xEventGroupClearBits(g_system_events, MQTT_CONNECTED_BIT);
            break;
            
        case MQTT_CLIENT_MANAGER_EVENT_RECONNECTED:
            ESP_LOGI(TAG, "Reconnected to MQTT broker");
            xEventGroupSetBits(g_system_events, MQTT_CONNECTED_BIT);
            break;
            
        case MQTT_CLIENT_MANAGER_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error occurred");
            break;
            
        default:
            break;
    }
}

/**
 * @brief Message handler for device commands and configuration updates
 */
static void mqtt_integration_message_handler(const mqtt_client_manager_message_t* message, void* user_data)
{
    ESP_LOGI(TAG, "Message received on '%s': %.*s", 
             message->topic, (int)message->payload_len, message->payload);
    
    /* Handle device commands */
    if (strstr(message->topic, "device/") && strstr(message->topic, "/command")) {
        cJSON* json = cJSON_Parse(message->payload);
        if (json != NULL) {
            cJSON* command = cJSON_GetObjectItem(json, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "Processing command: %s", command->valuestring);
                
                if (strcmp(command->valuestring, "get_status") == 0) {
                    /* Send device status */
                    char status_payload[512];
                    snprintf(status_payload, sizeof(status_payload),
                            "{"
                            "\"device_id\":\"%s\","
                            "\"uptime\":%lld,"
                            "\"free_heap\":%d,"
                            "\"wifi_connected\":%s,"
                            "\"mqtt_connected\":%s,"
                            "\"sensor_readings\":%d"
                            "}",
                            g_app_config.mqtt_client_id,
                            esp_timer_get_time() / 1000000,
                            esp_get_free_heap_size(),
                            wifi_manager_is_connected() ? "true" : "false",
                            (xEventGroupGetBits(g_system_events) & MQTT_CONNECTED_BIT) ? "true" : "false",
                            g_sensor_reading_count);
                    
                    mqtt_client_manager_publish_json("device/status", status_payload,
                                                    MQTT_CLIENT_MANAGER_QOS_1, false);
                    
                } else if (strcmp(command->valuestring, "restart") == 0) {
                    ESP_LOGW(TAG, "Restart command received");
                    mqtt_client_manager_publish_simple("device/status", "{\"status\":\"restarting\"}");
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    esp_restart();
                }
            }
            cJSON_Delete(json);
        }
    }
    
    /* Handle configuration updates */
    if (strstr(message->topic, "system/config/update")) {
        ESP_LOGI(TAG, "Configuration update received");
        
        cJSON* json = cJSON_Parse(message->payload);
        if (json != NULL) {
            bool config_changed = false;
            
            /* Update MQTT settings */
            cJSON* mqtt_enabled = cJSON_GetObjectItem(json, "mqtt_enabled");
            if (cJSON_IsBool(mqtt_enabled)) {
                g_app_config.mqtt_enabled = cJSON_IsTrue(mqtt_enabled);
                config_changed = true;
            }
            
            cJSON* publish_interval = cJSON_GetObjectItem(json, "publish_interval");
            if (cJSON_IsNumber(publish_interval)) {
                g_app_config.publish_interval_ms = publish_interval->valueint;
                g_publish_interval_ms = g_app_config.publish_interval_ms;
                config_changed = true;
            }
            
            if (config_changed) {
                save_application_config();
                ESP_LOGI(TAG, "Configuration updated and saved");
                
                mqtt_client_manager_publish_simple("system/config/status", 
                                                  "{\"status\":\"updated\"}");
            }
            
            cJSON_Delete(json);
        }
    }
}

/**
 * @brief Setup MQTT client with full integration
 */
static esp_err_t setup_mqtt_integration(void)
{
    if (!g_app_config.mqtt_enabled) {
        ESP_LOGI(TAG, "MQTT disabled in configuration");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Setting up integrated MQTT client...");
    
    /* Initialize MQTT Client Manager */
    esp_err_t ret = mqtt_client_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client manager: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Get default configuration and apply saved settings */
    mqtt_client_manager_config_t config;
    ret = mqtt_client_manager_get_default_config(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get default MQTT configuration");
        return ret;
    }
    
    /* Apply application configuration */
    strncpy(config.broker.broker_url, g_app_config.mqtt_broker_url, sizeof(config.broker.broker_url) - 1);
    config.broker.port = g_app_config.mqtt_port;
    strncpy(config.broker.client_id, g_app_config.mqtt_client_id, sizeof(config.broker.client_id) - 1);
    
    if (strlen(g_app_config.mqtt_username) > 0) {
        strncpy(config.broker.username, g_app_config.mqtt_username, sizeof(config.broker.username) - 1);
    }
    if (strlen(g_app_config.mqtt_password) > 0) {
        strncpy(config.broker.password, g_app_config.mqtt_password, sizeof(config.broker.password) - 1);
    }
    
    /* SSL/TLS configuration */
    if (g_app_config.mqtt_ssl_enabled) {
        config.broker.transport = MQTT_CLIENT_MANAGER_TRANSPORT_SSL;
        config.ssl.enabled = true;
        strncpy(config.ssl.ca_cert_path, "/spiffs/ca_cert.pem", sizeof(config.ssl.ca_cert_path) - 1);
        config.ssl.verify_peer = true;
        config.ssl.verify_hostname = true;
    }
    
    /* Advanced features */
    config.default_qos = g_app_config.mqtt_default_qos;
    config.enable_message_queue = true;
    config.message_queue_size = 30;
    config.enable_metrics = true;
    
    /* Last Will and Testament */
    config.will.enabled = true;
    strncpy(config.will.topic, "device/status", sizeof(config.will.topic) - 1);
    char will_message[128];
    snprintf(will_message, sizeof(will_message),
            "{\"device_id\":\"%s\",\"status\":\"offline\",\"reason\":\"connection_lost\"}",
            g_app_config.mqtt_client_id);
    config.will.message = will_message;
    config.will.message_len = strlen(will_message);
    config.will.qos = MQTT_CLIENT_MANAGER_QOS_1;
    config.will.retain = true;
    
    /* Apply configuration */
    ret = mqtt_client_manager_set_config(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set MQTT configuration");
        return ret;
    }
    
    /* Register event handlers */
    ret = mqtt_client_manager_register_event_callback(mqtt_integration_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler");
        return ret;
    }
    
    ret = mqtt_client_manager_register_message_callback(mqtt_integration_message_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT message handler");
        return ret;
    }
    
    ESP_LOGI(TAG, "MQTT integration setup complete");
    return ESP_OK;
}

/* ============================= SENSOR DATA PUBLISHING ============================= */

/**
 * @brief Task that publishes integrated sensor data
 */
static void integrated_sensor_task(void* pvParameters)
{
    ESP_LOGI(TAG, "Starting integrated sensor publishing task...");
    
    TickType_t last_publish = xTaskGetTickCount();
    
    while (1) {
        /* Wait for all systems to be ready */
        EventBits_t bits = xEventGroupWaitBits(g_system_events,
                                              SYSTEM_READY_BIT | WIFI_CONNECTED_BIT,
                                              pdFALSE, pdTRUE,
                                              pdMS_TO_TICKS(5000));
        
        if ((bits & (SYSTEM_READY_BIT | WIFI_CONNECTED_BIT)) != (SYSTEM_READY_BIT | WIFI_CONNECTED_BIT)) {
            ESP_LOGD(TAG, "System not ready for sensor publishing");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        
        /* Wait for publish interval */
        vTaskDelayUntil(&last_publish, pdMS_TO_TICKS(g_publish_interval_ms));
        
        if (g_mqtt_enabled && (xEventGroupGetBits(g_system_events) & MQTT_CONNECTED_BIT)) {
            g_sensor_reading_count++;
            
            /* Generate comprehensive sensor data */
            float temperature = 23.0f + (esp_random() % 800) / 100.0f;    /* 23.0 - 31.0°C */
            float humidity = 35.0f + (esp_random() % 4500) / 100.0f;      /* 35.0 - 80.0% */
            float pressure = 1013.25f + (esp_random() % 5000) / 100.0f;   /* 1013.25 - 1063.25 hPa */
            
            /* Create comprehensive sensor payload */
            char sensor_payload[1024];
            snprintf(sensor_payload, sizeof(sensor_payload),
                    "{"
                    "\"device_info\":{"
                        "\"device_id\":\"%s\","
                        "\"firmware_version\":\"1.0.0\","
                        "\"timestamp\":%lld"
                    "},"
                    "\"sensors\":{"
                        "\"temperature\":{\"value\":%.2f,\"unit\":\"celsius\"},"
                        "\"humidity\":{\"value\":%.2f,\"unit\":\"percent\"},"
                        "\"pressure\":{\"value\":%.2f,\"unit\":\"hPa\"}"
                    "},"
                    "\"system\":{"
                        "\"uptime\":%lld,"
                        "\"free_heap\":%d,"
                        "\"reading_count\":%d,"
                        "\"wifi_rssi\":%d"
                    "},"
                    "\"configuration\":{"
                        "\"publish_interval\":%d,"
                        "\"ssl_enabled\":%s,"
                        "\"default_qos\":%d"
                    "}"
                    "}",
                    g_app_config.mqtt_client_id,
                    esp_timer_get_time() / 1000,
                    temperature, humidity, pressure,
                    esp_timer_get_time() / 1000000,
                    esp_get_free_heap_size(),
                    g_sensor_reading_count,
                    wifi_manager_get_rssi(),
                    g_app_config.publish_interval_ms,
                    g_app_config.mqtt_ssl_enabled ? "true" : "false",
                    g_app_config.mqtt_default_qos);
            
            /* Publish comprehensive sensor data */
            int msg_id = mqtt_client_manager_publish_json("sensors/comprehensive",
                                                        sensor_payload,
                                                        g_app_config.mqtt_default_qos, false);
            
            if (msg_id >= 0) {
                ESP_LOGI(TAG, "Published sensor data: T=%.2f°C, H=%.2f%%, P=%.2fhPa (reading: %d, msg_id: %d)",
                         temperature, humidity, pressure, g_sensor_reading_count, msg_id);
            } else {
                ESP_LOGE(TAG, "Failed to publish sensor data");
            }
            
            /* Publish individual sensor values for specific subscribers */
            mqtt_client_manager_publish_formatted("sensors/temperature",
                                                 MQTT_CLIENT_MANAGER_QOS_0, false,
                                                 "%.2f", temperature);
            
            mqtt_client_manager_publish_formatted("sensors/humidity",
                                                 MQTT_CLIENT_MANAGER_QOS_0, false,
                                                 "%.2f", humidity);
            
            mqtt_client_manager_publish_formatted("sensors/pressure",
                                                 MQTT_CLIENT_MANAGER_QOS_0, false,
                                                 "%.2f", pressure);
            
        } else {
            ESP_LOGD(TAG, "MQTT not available for sensor publishing");
        }
    }
}

/* ============================= SYSTEM INITIALIZATION ============================= */

/**
 * @brief Initialize all system components
 */
static esp_err_t initialize_system(void)
{
    ESP_LOGI(TAG, "Initializing complete system...");
    
    /* Create event group */
    g_system_events = xEventGroupCreate();
    if (g_system_events == NULL) {
        ESP_LOGE(TAG, "Failed to create system event group");
        return ESP_ERR_NO_MEM;
    }
    
    /* Initialize SPIFFS File Manager */
    esp_err_t ret = spiffs_file_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Initialize Config Manager */
    ret = config_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Config Manager: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Load application configuration */
    ret = load_application_config();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load application configuration");
        return ret;
    }
    
    /* Initialize WiFi Manager */
    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi Manager: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Initialize AP Manager for configuration portal */
    ret = ap_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize AP Manager: %s", esp_err_to_name(ret));
        return ret;
    }
    
    xEventGroupSetBits(g_system_events, SYSTEM_READY_BIT);
    ESP_LOGI(TAG, "System initialization complete");
    
    return ESP_OK;
}

/**
 * @brief System monitoring task
 */
static void system_monitor_task(void* pvParameters)
{
    ESP_LOGI(TAG, "Starting system monitoring task...");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000)); /* Monitor every minute */
        
        /* Check WiFi status */
        if (wifi_manager_is_connected()) {
            if (!(xEventGroupGetBits(g_system_events) & WIFI_CONNECTED_BIT)) {
                ESP_LOGI(TAG, "WiFi connected");
                xEventGroupSetBits(g_system_events, WIFI_CONNECTED_BIT);
            }
        } else {
            if (xEventGroupGetBits(g_system_events) & WIFI_CONNECTED_BIT) {
                ESP_LOGW(TAG, "WiFi disconnected");
                xEventGroupClearBits(g_system_events, WIFI_CONNECTED_BIT | MQTT_CONNECTED_BIT);
            }
        }
        
        /* Print system status */
        ESP_LOGI(TAG, "=== System Status ===");
        ESP_LOGI(TAG, "WiFi: %s", wifi_manager_is_connected() ? "Connected" : "Disconnected");
        ESP_LOGI(TAG, "MQTT: %s", (xEventGroupGetBits(g_system_events) & MQTT_CONNECTED_BIT) ? "Connected" : "Disconnected");
        ESP_LOGI(TAG, "Free Heap: %d bytes", esp_get_free_heap_size());
        ESP_LOGI(TAG, "Uptime: %lld seconds", esp_timer_get_time() / 1000000);
        ESP_LOGI(TAG, "Sensor readings: %d", g_sensor_reading_count);
        ESP_LOGI(TAG, "====================");
    }
}

/* ============================= MAIN APPLICATION ============================= */

/**
 * @brief Main application entry point
 */
void app_main(void)
{
    ESP_LOGI(TAG, "Starting Complete MQTT Integration Example");
    
    /* Initialize system components */
    esp_err_t ret = initialize_system();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize system components");
        return;
    }
    
    /* Wait for WiFi connection */
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    while (!wifi_manager_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    xEventGroupSetBits(g_system_events, WIFI_CONNECTED_BIT);
    ESP_LOGI(TAG, "WiFi connected!");
    
    /* Setup MQTT integration */
    ret = setup_mqtt_integration();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to setup MQTT integration");
        return;
    }
    
    /* Connect to MQTT broker if enabled */
    if (g_app_config.mqtt_enabled) {
        ESP_LOGI(TAG, "Connecting to MQTT broker...");
        ret = mqtt_client_manager_connect();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initiate MQTT connection");
        }
    } else {
        ESP_LOGI(TAG, "MQTT disabled, skipping connection");
    }
    
    /* Create application tasks */
    xTaskCreate(integrated_sensor_task, "integrated_sensors", 6144, NULL, 5, NULL);
    xTaskCreate(system_monitor_task, "system_monitor", 4096, NULL, 3, NULL);
    
    ESP_LOGI(TAG, "Complete MQTT Integration Example started successfully");
    
    /* Main application loop */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000)); /* Main loop every 30 seconds */
        
        /* Perform periodic maintenance tasks */
        if (g_mqtt_enabled && mqtt_client_manager_is_initialized()) {
            /* Print MQTT statistics */
            mqtt_client_manager_stats_t stats;
            if (mqtt_client_manager_get_statistics(&stats) == ESP_OK) {
                ESP_LOGD(TAG, "MQTT Stats - Pub: %d, Rec: %d, Failed: %d, Reconnects: %d",
                         stats.messages_published, stats.messages_received,
                         stats.messages_failed, stats.reconnections);
            }
        }
        
        /* Check memory usage */
        size_t free_heap = esp_get_free_heap_size();
        if (free_heap < 50000) {  /* Less than 50KB */
            ESP_LOGW(TAG, "Low memory warning: %zu bytes free", free_heap);
        }
    }
}