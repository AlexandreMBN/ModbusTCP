/**
 * @file mqtt_ssl_example.c
 * @brief MQTT Client Manager SSL/TLS Example
 * 
 * This example demonstrates secure MQTT communication using SSL/TLS:
 * - SSL/TLS configuration with certificates
 * - Secure connection to MQTT broker
 * - Certificate validation and error handling
 * - Integration with Config Manager for certificate storage
 * 
 * @version 1.0.0
 * @date 2024-11-10
 */

#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <esp_spiffs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/* Include the MQTT Client Manager library */
#include "mqtt_client_manager.h"

/* Include other required libraries for integration */
#include "wifi_manager.h"
#include "config_manager.h"
#include "spiffs_file_manager.h"

#define TAG "MQTT_SSL_EXAMPLE"

/* SSL/TLS Configuration */
#define MQTT_BROKER_URL        "mqtts://test.mosquitto.org"
#define MQTT_BROKER_PORT       8883
#define MQTT_CLIENT_ID         "esp32_ssl_example"
#define CA_CERT_PATH           "/spiffs/ca_cert.pem"
#define CLIENT_CERT_PATH       "/spiffs/client_cert.pem"
#define CLIENT_KEY_PATH        "/spiffs/client_key.pem"

/* Application configuration */
#define PUBLISH_INTERVAL_MS    60000   /* Publish every minute */

/* Global variables */
static bool g_mqtt_connected = false;
static bool g_ssl_enabled = true;

/* ============================= CA CERTIFICATE ============================= */

/* Example CA certificate for test.mosquitto.org (Let's Encrypt)
   In production, load this from SPIFFS or embed it properly */
static const char* ca_cert_pem = \
"-----BEGIN CERTIFICATE-----\r\n"
"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\r\n"
"-----END CERTIFICATE-----";

/* ============================= CERTIFICATE MANAGEMENT ============================= */

/**
 * @brief Load or create SSL certificates
 */
static esp_err_t setup_ssl_certificates(void)
{
    ESP_LOGI(TAG, "Setting up SSL certificates...");
    
    /* Check if CA certificate exists */
    if (!spiffs_file_manager_file_exists(CA_CERT_PATH)) {
        ESP_LOGI(TAG, "Creating CA certificate file");
        
        /* Save the embedded CA certificate to SPIFFS */
        esp_err_t ret = spiffs_file_manager_write_file(CA_CERT_PATH, ca_cert_pem, strlen(ca_cert_pem));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save CA certificate: %s", esp_err_to_name(ret));
            return ret;
        }
        
        ESP_LOGI(TAG, "CA certificate saved to SPIFFS");
    } else {
        ESP_LOGI(TAG, "CA certificate already exists");
    }
    
    /* Verify certificate file is readable */
    size_t file_size;
    esp_err_t ret = spiffs_file_manager_get_file_size(CA_CERT_PATH, &file_size);
    if (ret != ESP_OK || file_size == 0) {
        ESP_LOGE(TAG, "CA certificate file is not readable or empty");
        return ESP_ERR_INVALID_SIZE;
    }
    
    ESP_LOGI(TAG, "CA certificate ready (size: %zu bytes)", file_size);
    return ESP_OK;
}

/* ============================= MQTT EVENT HANDLERS ============================= */

/**
 * @brief MQTT event handler for SSL example
 */
static void mqtt_ssl_event_handler(mqtt_client_manager_event_t event, void* data, void* user_data)
{
    switch (event) {
        case MQTT_CLIENT_MANAGER_EVENT_BEFORE_CONNECT:
            ESP_LOGI(TAG, "Connecting to MQTT broker with SSL/TLS...");
            break;
            
        case MQTT_CLIENT_MANAGER_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Securely connected to MQTT broker!");
            g_mqtt_connected = true;
            
            /* Subscribe to secure topics */
            mqtt_client_manager_subscribe("secure/sensors/+", MQTT_CLIENT_MANAGER_QOS_1);
            mqtt_client_manager_subscribe("secure/commands/device", MQTT_CLIENT_MANAGER_QOS_2);
            
            /* Publish connection announcement */
            mqtt_client_manager_publish_json("secure/devices/online", 
                                            "{\"device_id\":\"" MQTT_CLIENT_ID "\","
                                            "\"ssl_enabled\":true,"
                                            "\"timestamp\":" "0" "}",
                                            MQTT_CLIENT_MANAGER_QOS_1, true);
            break;
            
        case MQTT_CLIENT_MANAGER_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Disconnected from secure MQTT broker");
            g_mqtt_connected = false;
            break;
            
        case MQTT_CLIENT_MANAGER_EVENT_CONNECTION_LOST:
            ESP_LOGW(TAG, "Secure connection lost, will auto-reconnect");
            g_mqtt_connected = false;
            break;
            
        case MQTT_CLIENT_MANAGER_EVENT_RECONNECTED:
            ESP_LOGI(TAG, "Securely reconnected to MQTT broker");
            g_mqtt_connected = true;
            break;
            
        case MQTT_CLIENT_MANAGER_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT SSL error occurred");
            
            /* Log additional SSL error details if available */
            mqtt_client_manager_status_t status;
            if (mqtt_client_manager_get_status(&status) == ESP_OK) {
                ESP_LOGE(TAG, "Status: %s", status.status_message);
            }
            break;
            
        default:
            ESP_LOGD(TAG, "MQTT SSL event: %d", event);
            break;
    }
}

/**
 * @brief Message handler for secure topics
 */
static void mqtt_ssl_message_handler(const mqtt_client_manager_message_t* message, void* user_data)
{
    ESP_LOGI(TAG, "Secure message received on '%s': %.*s", 
             message->topic, (int)message->payload_len, message->payload);
    
    /* Process secure commands */
    if (strstr(message->topic, "secure/commands/device")) {
        ESP_LOGI(TAG, "Processing secure command: %.*s", (int)message->payload_len, message->payload);
        
        /* Parse command and respond securely */
        mqtt_client_manager_publish_formatted("secure/responses/device",
                                             MQTT_CLIENT_MANAGER_QOS_1, false,
                                             "{\"status\":\"executed\","
                                             "\"command\":\"%.*s\","
                                             "\"ssl_verified\":true,"
                                             "\"timestamp\":%lld}",
                                             (int)message->payload_len, message->payload,
                                             esp_timer_get_time() / 1000);
    }
}

/* ============================= MQTT SSL SETUP ============================= */

/**
 * @brief Configure MQTT client with SSL/TLS
 */
static esp_err_t setup_mqtt_ssl_client(void)
{
    ESP_LOGI(TAG, "Setting up MQTT SSL client...");
    
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
    config.broker.port = MQTT_BROKER_PORT;
    config.broker.transport = MQTT_CLIENT_MANAGER_TRANSPORT_SSL;
    config.broker.keepalive = 60;
    config.broker.clean_session = true;
    config.broker.auto_reconnect = true;
    config.broker.reconnect_timeout_ms = 10000; /* Longer timeout for SSL */
    config.broker.connect_timeout_ms = 15000;   /* Longer timeout for SSL handshake */
    
    /* SSL/TLS configuration */
    config.ssl.enabled = true;
    strncpy(config.ssl.ca_cert_path, CA_CERT_PATH, sizeof(config.ssl.ca_cert_path) - 1);
    config.ssl.verify_peer = true;
    config.ssl.verify_hostname = true;
    config.ssl.skip_cert_common_name_check = false;
    config.ssl.use_global_ca_store = false;
    
    /* Optional: Client certificate authentication (uncomment if needed) */
    /*
    strncpy(config.ssl.client_cert_path, CLIENT_CERT_PATH, sizeof(config.ssl.client_cert_path) - 1);
    strncpy(config.ssl.client_key_path, CLIENT_KEY_PATH, sizeof(config.ssl.client_key_path) - 1);
    */
    
    /* Last Will and Testament for secure disconnection detection */
    config.will.enabled = true;
    strncpy(config.will.topic, "secure/devices/offline", sizeof(config.will.topic) - 1);
    config.will.message = "{\"device_id\":\"" MQTT_CLIENT_ID "\",\"reason\":\"connection_lost\"}";
    config.will.message_len = strlen(config.will.message);
    config.will.qos = MQTT_CLIENT_MANAGER_QOS_1;
    config.will.retain = true;
    
    /* Enhanced security features */
    config.enable_message_queue = true;
    config.message_queue_size = 20;
    config.default_qos = MQTT_CLIENT_MANAGER_QOS_1; /* Higher QoS for security */
    config.enable_metrics = true;
    
    /* Apply configuration */
    ret = mqtt_client_manager_set_config(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set MQTT SSL configuration");
        return ret;
    }
    
    /* Register event handlers */
    ret = mqtt_client_manager_register_event_callback(mqtt_ssl_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler");
        return ret;
    }
    
    ret = mqtt_client_manager_register_message_callback(mqtt_ssl_message_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT message handler");
        return ret;
    }
    
    ESP_LOGI(TAG, "MQTT SSL client setup complete");
    return ESP_OK;
}

/* ============================= SECURE PUBLISHING TASK ============================= */

/**
 * @brief Task that publishes encrypted sensor data periodically
 */
static void secure_publish_task(void* pvParameters)
{
    ESP_LOGI(TAG, "Starting secure publish task...");
    
    TickType_t last_publish = xTaskGetTickCount();
    uint32_t sequence_number = 0;
    
    while (1) {
        /* Wait for publish interval */
        vTaskDelayUntil(&last_publish, pdMS_TO_TICKS(PUBLISH_INTERVAL_MS));
        
        if (g_mqtt_connected) {
            sequence_number++;
            
            /* Generate secure sensor data with additional metadata */
            float temperature = 22.5f + (esp_random() % 500) / 100.0f;  /* 22.5 - 27.5°C */
            float humidity = 45.0f + (esp_random() % 3000) / 100.0f;    /* 45.0 - 75.0% */
            
            /* Create secure JSON payload with integrity information */
            char secure_payload[512];
            uint32_t checksum = (uint32_t)(temperature * 1000) + (uint32_t)(humidity * 1000) + sequence_number;
            
            snprintf(secure_payload, sizeof(secure_payload),
                    "{"
                    "\"device_id\":\"%s\","
                    "\"sequence\":%d,"
                    "\"timestamp\":%lld,"
                    "\"sensors\":{"
                        "\"temperature\":%.2f,"
                        "\"humidity\":%.2f"
                    "},"
                    "\"security\":{"
                        "\"ssl_enabled\":true,"
                        "\"checksum\":%u,"
                        "\"protocol_version\":\"1.0\""
                    "},"
                    "\"system\":{"
                        "\"free_heap\":%d,"
                        "\"uptime\":%lld"
                    "}"
                    "}",
                    MQTT_CLIENT_ID,
                    sequence_number,
                    esp_timer_get_time() / 1000,
                    temperature,
                    humidity,
                    checksum,
                    esp_get_free_heap_size(),
                    esp_timer_get_time() / 1000000);
            
            /* Publish secure sensor data */
            int msg_id = mqtt_client_manager_publish_json("secure/sensors/environmental",
                                                        secure_payload,
                                                        MQTT_CLIENT_MANAGER_QOS_1, false);
            
            if (msg_id >= 0) {
                ESP_LOGI(TAG, "Published secure data: T=%.2f°C, H=%.2f%% (seq: %d, msg_id: %d)", 
                         temperature, humidity, sequence_number, msg_id);
            } else {
                ESP_LOGE(TAG, "Failed to publish secure sensor data");
            }
            
            /* Publish heartbeat */
            int heartbeat_id = mqtt_client_manager_publish_formatted("secure/devices/heartbeat",
                                                                   MQTT_CLIENT_MANAGER_QOS_0, false,
                                                                   "{\"device_id\":\"%s\",\"timestamp\":%lld}",
                                                                   MQTT_CLIENT_ID,
                                                                   esp_timer_get_time() / 1000);
            
            if (heartbeat_id >= 0) {
                ESP_LOGD(TAG, "Heartbeat sent (msg_id: %d)", heartbeat_id);
            }
            
        } else {
            ESP_LOGD(TAG, "MQTT SSL not connected, skipping publish");
        }
    }
}

/* ============================= MAIN APPLICATION ============================= */

/**
 * @brief Main application entry point for SSL example
 */
void app_main(void)
{
    ESP_LOGI(TAG, "Starting MQTT SSL/TLS Example");
    
    /* Initialize SPIFFS for certificate storage */
    ESP_LOGI(TAG, "Initializing SPIFFS...");
    esp_err_t ret = spiffs_file_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS: %s", esp_err_to_name(ret));
        return;
    }
    
    /* Setup SSL certificates */
    ret = setup_ssl_certificates();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to setup SSL certificates");
        return;
    }
    
    /* Initialize WiFi */
    ESP_LOGI(TAG, "Initializing WiFi...");
    ret = wifi_manager_init();
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
    
    /* Setup MQTT SSL client */
    ret = setup_mqtt_ssl_client();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to setup MQTT SSL client");
        return;
    }
    
    /* Connect to secure MQTT broker */
    ESP_LOGI(TAG, "Connecting to secure MQTT broker...");
    ret = mqtt_client_manager_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initiate secure MQTT connection");
        return;
    }
    
    /* Create secure application tasks */
    xTaskCreate(secure_publish_task, "mqtt_ssl_publish", 6144, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "MQTT SSL Example started successfully");
    
    /* Main monitoring loop */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000)); /* Check every 30 seconds */
        
        if (mqtt_client_manager_is_initialized()) {
            mqtt_client_manager_status_t status;
            if (mqtt_client_manager_get_status(&status) == ESP_OK) {
                ESP_LOGI(TAG, "SSL Status: %s | Connected: %s | Transport: %s", 
                         status.status_message,
                         status.connected ? "Yes" : "No",
                         mqtt_client_manager_transport_to_string(status.transport));
                
                if (status.connected) {
                    ESP_LOGI(TAG, "Secure connection to: %s:%d", status.broker_url, status.broker_port);
                }
            }
        }
        
        ESP_LOGD(TAG, "Main loop - Free heap: %d bytes", esp_get_free_heap_size());
    }
}