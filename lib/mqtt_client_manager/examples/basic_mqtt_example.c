/**
 * @file basic_mqtt_example.c
 * @brief Basic MQTT Client Manager Example
 * 
 * This example demonstrates basic usage of the MQTT Client Manager library:
 * - Initialization and configuration
 * - Connection to MQTT broker
 * - Publishing and subscribing to topics
 * - Event handling
 * 
 * @version 1.0.0
 * @date 2024-11-10
 */

#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/* Include the MQTT Client Manager library */
#include "mqtt_client_manager.h"

/* Include other required libraries for integration */
#include "wifi_manager.h"
#include "config_manager.h"

#define TAG "MQTT_BASIC_EXAMPLE"

/* Application configuration */
#define MQTT_BROKER_URL     "mqtt://broker.hivemq.com"
#define MQTT_CLIENT_ID      "esp32_basic_example"
#define PUBLISH_INTERVAL_MS 30000   /* Publish every 30 seconds */

/* Global variables */
static bool g_mqtt_connected = false;
static uint32_t g_message_count = 0;

/* ============================= EVENT HANDLERS ============================= */

/**
 * @brief MQTT event handler
 */
static void mqtt_event_handler(mqtt_client_manager_event_t event, void* data, void* user_data)
{
    switch (event) {
        case MQTT_CLIENT_MANAGER_EVENT_BEFORE_CONNECT:
            ESP_LOGI(TAG, "Connecting to MQTT broker...");
            break;
            
        case MQTT_CLIENT_MANAGER_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected to MQTT broker!");
            g_mqtt_connected = true;
            
            /* Subscribe to topics after connection */
            mqtt_client_manager_subscribe("sensors/temperature", MQTT_CLIENT_MANAGER_QOS_1);
            mqtt_client_manager_subscribe("sensors/humidity", MQTT_CLIENT_MANAGER_QOS_1);
            mqtt_client_manager_subscribe("commands/+", MQTT_CLIENT_MANAGER_QOS_1);
            break;
            
        case MQTT_CLIENT_MANAGER_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Disconnected from MQTT broker");
            g_mqtt_connected = false;
            break;
            
        case MQTT_CLIENT_MANAGER_EVENT_CONNECTION_LOST:
            ESP_LOGW(TAG, "Connection lost, will auto-reconnect");
            g_mqtt_connected = false;
            break;
            
        case MQTT_CLIENT_MANAGER_EVENT_RECONNECTED:
            ESP_LOGI(TAG, "Reconnected to MQTT broker");
            g_mqtt_connected = true;
            break;
            
        case MQTT_CLIENT_MANAGER_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error occurred");
            break;
            
        default:
            ESP_LOGD(TAG, "MQTT event: %d", event);
            break;
    }
}

/**
 * @brief Message received handler
 */
static void mqtt_message_handler(const mqtt_client_manager_message_t* message, void* user_data)
{
    ESP_LOGI(TAG, "Message received on topic '%s': %.*s", 
             message->topic, (int)message->payload_len, message->payload);
    
    /* Process different message types */
    if (strstr(message->topic, "temperature")) {
        float temperature = atof(message->payload);
        ESP_LOGI(TAG, "Temperature reading: %.2f°C", temperature);
    } else if (strstr(message->topic, "humidity")) {
        float humidity = atof(message->payload);
        ESP_LOGI(TAG, "Humidity reading: %.2f%%", humidity);
    } else if (strstr(message->topic, "commands/")) {
        ESP_LOGI(TAG, "Command received: %.*s", (int)message->payload_len, message->payload);
        
        /* Echo command response */
        char response_topic[128];
        snprintf(response_topic, sizeof(response_topic), "responses/%s", 
                message->topic + strlen("commands/"));
        
        mqtt_client_manager_publish_formatted(response_topic, 
                                             MQTT_CLIENT_MANAGER_QOS_1, false,
                                             "{\"status\":\"received\",\"command\":\"%.*s\",\"timestamp\":%lld}",
                                             (int)message->payload_len, message->payload,
                                             esp_timer_get_time() / 1000);
    }
}

/* ============================= MQTT SETUP ============================= */

/**
 * @brief Initialize and configure MQTT client
 */
static esp_err_t setup_mqtt_client(void)
{
    ESP_LOGI(TAG, "Setting up MQTT client...");
    
    /* Initialize MQTT Client Manager */
    esp_err_t ret = mqtt_client_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client manager: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Get default configuration */
    mqtt_client_manager_config_t config;
    ret = mqtt_client_manager_get_default_config(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get default MQTT configuration");
        return ret;
    }
    
    /* Configure broker settings */
    strncpy(config.broker.broker_url, MQTT_BROKER_URL, sizeof(config.broker.broker_url) - 1);
    strncpy(config.broker.client_id, MQTT_CLIENT_ID, sizeof(config.broker.client_id) - 1);
    config.broker.port = 1883;
    config.broker.keepalive = 60;
    config.broker.clean_session = true;
    config.broker.auto_reconnect = true;
    config.broker.reconnect_timeout_ms = 5000;
    
    /* Enable features */
    config.enable_message_queue = true;
    config.message_queue_size = 10;
    config.default_qos = MQTT_CLIENT_MANAGER_QOS_1;
    config.enable_metrics = true;
    
    /* Apply configuration */
    ret = mqtt_client_manager_set_config(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set MQTT configuration");
        return ret;
    }
    
    /* Register event handlers */
    ret = mqtt_client_manager_register_event_callback(mqtt_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler");
        return ret;
    }
    
    ret = mqtt_client_manager_register_message_callback(mqtt_message_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT message handler");
        return ret;
    }
    
    ESP_LOGI(TAG, "MQTT client setup complete");
    return ESP_OK;
}

/* ============================= PUBLISHING TASK ============================= */

/**
 * @brief Task that publishes sensor data periodically
 */
static void publish_task(void* pvParameters)
{
    ESP_LOGI(TAG, "Starting publish task...");
    
    TickType_t last_publish = xTaskGetTickCount();
    
    while (1) {
        /* Wait for publish interval */
        vTaskDelayUntil(&last_publish, pdMS_TO_TICKS(PUBLISH_INTERVAL_MS));
        
        if (g_mqtt_connected) {
            g_message_count++;
            
            /* Publish simulated sensor data */
            float temperature = 20.0f + (esp_random() % 1000) / 100.0f;  /* 20.0 - 30.0°C */
            float humidity = 40.0f + (esp_random() % 4000) / 100.0f;     /* 40.0 - 80.0% */
            
            /* Publish temperature */
            int temp_msg_id = mqtt_client_manager_publish_formatted("sensors/temperature",
                                                                   MQTT_CLIENT_MANAGER_QOS_1, false,
                                                                   "%.2f", temperature);
            
            if (temp_msg_id >= 0) {
                ESP_LOGI(TAG, "Published temperature: %.2f°C (msg_id: %d)", temperature, temp_msg_id);
            } else {
                ESP_LOGE(TAG, "Failed to publish temperature");
            }
            
            /* Publish humidity */
            int hum_msg_id = mqtt_client_manager_publish_formatted("sensors/humidity",
                                                                  MQTT_CLIENT_MANAGER_QOS_1, false,
                                                                  "%.2f", humidity);
            
            if (hum_msg_id >= 0) {
                ESP_LOGI(TAG, "Published humidity: %.2f%% (msg_id: %d)", humidity, hum_msg_id);
            } else {
                ESP_LOGE(TAG, "Failed to publish humidity");
            }
            
            /* Publish status message with JSON */
            char status_json[256];
            snprintf(status_json, sizeof(status_json),
                    "{"
                    "\"device_id\":\"%s\","
                    "\"uptime\":%d,"
                    "\"message_count\":%d,"
                    "\"free_heap\":%d,"
                    "\"timestamp\":%lld"
                    "}",
                    MQTT_CLIENT_ID,
                    (int)(esp_timer_get_time() / 1000000), /* uptime in seconds */
                    g_message_count,
                    esp_get_free_heap_size(),
                    esp_timer_get_time() / 1000); /* timestamp in ms */
            
            int status_msg_id = mqtt_client_manager_publish_json("device/status", status_json,
                                                               MQTT_CLIENT_MANAGER_QOS_0, false);
            
            if (status_msg_id >= 0) {
                ESP_LOGD(TAG, "Published status (msg_id: %d)", status_msg_id);
            }
            
        } else {
            ESP_LOGD(TAG, "MQTT not connected, skipping publish");
        }
    }
}

/* ============================= STATISTICS TASK ============================= */

/**
 * @brief Task that prints MQTT statistics periodically
 */
static void statistics_task(void* pvParameters)
{
    ESP_LOGI(TAG, "Starting statistics task...");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000)); /* Print stats every minute */
        
        if (mqtt_client_manager_is_initialized()) {
            mqtt_client_manager_stats_t stats;
            esp_err_t ret = mqtt_client_manager_get_statistics(&stats);
            
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "=== MQTT Statistics ===");
                ESP_LOGI(TAG, "Messages published: %d", stats.messages_published);
                ESP_LOGI(TAG, "Messages received: %d", stats.messages_received);
                ESP_LOGI(TAG, "Messages failed: %d", stats.messages_failed);
                ESP_LOGI(TAG, "Connection attempts: %d", stats.connection_attempts);
                ESP_LOGI(TAG, "Successful connections: %d", stats.successful_connections);
                ESP_LOGI(TAG, "Disconnections: %d", stats.disconnections);
                ESP_LOGI(TAG, "Reconnections: %d", stats.reconnections);
                ESP_LOGI(TAG, "Active subscriptions: %d", stats.subscription_count);
                ESP_LOGI(TAG, "Uptime: %d ms", stats.uptime_ms);
                ESP_LOGI(TAG, "Bytes sent: %zu", stats.bytes_sent);
                ESP_LOGI(TAG, "Bytes received: %zu", stats.bytes_received);
                ESP_LOGI(TAG, "=======================");
            }
            
            mqtt_client_manager_status_t status;
            ret = mqtt_client_manager_get_status(&status);
            
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Current state: %s", mqtt_client_manager_state_to_string(status.state));
                ESP_LOGI(TAG, "Status message: %s", status.status_message);
                if (status.connected) {
                    ESP_LOGI(TAG, "Connected to: %s:%d", status.broker_url, status.broker_port);
                    ESP_LOGI(TAG, "Transport: %s", mqtt_client_manager_transport_to_string(status.transport));
                }
            }
        }
    }
}

/* ============================= MAIN APPLICATION ============================= */

/**
 * @brief Main application entry point
 */
void app_main(void)
{
    ESP_LOGI(TAG, "Starting Basic MQTT Example");
    
    /* Initialize WiFi (required for MQTT) */
    ESP_LOGI(TAG, "Initializing WiFi...");
    esp_err_t ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi manager");
        return;
    }
    
    /* Wait for WiFi connection */
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    while (!wifi_manager_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG, "WiFi connected!");
    
    /* Setup MQTT client */
    ret = setup_mqtt_client();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to setup MQTT client");
        return;
    }
    
    /* Connect to MQTT broker */
    ESP_LOGI(TAG, "Connecting to MQTT broker...");
    ret = mqtt_client_manager_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initiate MQTT connection");
        return;
    }
    
    /* Create application tasks */
    xTaskCreate(publish_task, "mqtt_publish", 4096, NULL, 5, NULL);
    xTaskCreate(statistics_task, "mqtt_stats", 3072, NULL, 3, NULL);
    
    ESP_LOGI(TAG, "Basic MQTT Example started successfully");
    
    /* Main loop */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        
        /* Application could perform other tasks here */
        ESP_LOGD(TAG, "Main loop - Free heap: %d bytes", esp_get_free_heap_size());
    }
}