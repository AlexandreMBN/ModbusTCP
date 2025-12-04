/**
 * @file mqtt_client_manager.c
 * @brief Comprehensive MQTT Client Manager Library Implementation for ESP32
 * 
 * This library provides a complete MQTT client solution for ESP32 projects with
 * advanced connection management, SSL/TLS support, and event-driven architecture.
 * 
 * @version 1.0.0
 * @date 2024-11-10
 * @author ESP32 Development Team
 * 
 * @copyright Copyright (c) 2024 ESP32 Development Team
 * Licensed under the MIT License.
 */

#include "mqtt_client_manager.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_spiffs.h>
#include <cJSON.h>
#include <mqtt_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>

/* ============================= PRIVATE CONSTANTS ============================= */

#define TAG "MQTT_CLIENT_MANAGER"

/* Event group bits */
#define MQTT_CONNECTED_BIT          BIT0
#define MQTT_DISCONNECTED_BIT       BIT1
#define MQTT_RECONNECTING_BIT       BIT2
#define MQTT_ERROR_BIT              BIT3

/* Task parameters */
#define MQTT_CLIENT_MANAGER_TASK_STACK_SIZE  8192
#define MQTT_CLIENT_MANAGER_TASK_PRIORITY    5
#define MQTT_CLIENT_MANAGER_QUEUE_SIZE       50

/* Timeouts and intervals */
#define MQTT_CLIENT_MANAGER_MAX_WAIT_MS      5000
#define MQTT_CLIENT_MANAGER_WATCHDOG_MS      30000
#define MQTT_CLIENT_MANAGER_STATS_UPDATE_MS  1000

/* String constants */
#define MQTT_CLIENT_MANAGER_CONFIG_FILE      "/spiffs/mqtt_config.json"
#define MQTT_CLIENT_MANAGER_STATE_FILE       "/spiffs/mqtt_state.json"

/* ============================= PRIVATE STRUCTURES ============================= */

/**
 * @brief Internal message queue item
 */
typedef struct {
    mqtt_client_manager_message_t* message;
    uint32_t timestamp_ms;
    uint8_t retry_count;
    bool persistent;
} mqtt_client_manager_queue_item_t;

/**
 * @brief Event callback registration
 */
typedef struct mqtt_client_manager_event_callback_node {
    mqtt_client_manager_event_callback_t callback;
    void* user_data;
    struct mqtt_client_manager_event_callback_node* next;
} mqtt_client_manager_event_callback_node_t;

/**
 * @brief Message callback registration
 */
typedef struct mqtt_client_manager_message_callback_node {
    mqtt_client_manager_message_callback_t callback;
    void* user_data;
    struct mqtt_client_manager_message_callback_node* next;
} mqtt_client_manager_message_callback_node_t;

/**
 * @brief Topic callback registration
 */
typedef struct mqtt_client_manager_topic_callback_node {
    char topic[MQTT_CLIENT_MANAGER_MAX_TOPIC_LEN];
    mqtt_client_manager_topic_callback_t callback;
    void* user_data;
    struct mqtt_client_manager_topic_callback_node* next;
} mqtt_client_manager_topic_callback_node_t;

/**
 * @brief Internal client context
 */
typedef struct {
    /* Core components */
    esp_mqtt_client_handle_t mqtt_client;
    mqtt_client_manager_config_t config;
    mqtt_client_manager_status_t status;
    
    /* Task management */
    TaskHandle_t task_handle;
    QueueHandle_t message_queue;
    QueueHandle_t internal_queue;
    EventGroupHandle_t event_group;
    SemaphoreHandle_t mutex;
    
    /* State management */
    bool initialized;
    bool running;
    uint32_t connection_start_time;
    uint32_t last_activity_time;
    uint32_t reconnect_attempts;
    uint32_t next_reconnect_time;
    
    /* Subscriptions */
    mqtt_client_manager_subscription_t subscriptions[MQTT_CLIENT_MANAGER_MAX_SUBSCRIPTIONS];
    size_t subscription_count;
    
    /* Callbacks */
    mqtt_client_manager_event_callback_node_t* event_callbacks;
    mqtt_client_manager_message_callback_node_t* message_callbacks;
    mqtt_client_manager_topic_callback_node_t* topic_callbacks;
    
    /* Message tracking */
    int next_message_id;
    uint32_t pending_messages[32];  /* Bit array for tracking pending messages */
    
    /* SSL/TLS resources */
    char* ca_cert_buffer;
    char* client_cert_buffer;
    char* client_key_buffer;
    
    /* Statistics timer */
    esp_timer_handle_t stats_timer;
} mqtt_client_manager_context_t;

/* ============================= PRIVATE VARIABLES ============================= */

static mqtt_client_manager_context_t* g_mqtt_context = NULL;

/* ============================= PRIVATE FUNCTION DECLARATIONS ============================= */

/* Core functions */
static esp_err_t mqtt_client_manager_create_context(void);
static esp_err_t mqtt_client_manager_destroy_context(void);
static void mqtt_client_manager_task(void* pvParameters);
static void mqtt_client_manager_stats_timer_callback(void* arg);

/* MQTT event handling */
static void mqtt_client_manager_event_handler(void* handler_args, esp_event_base_t base, 
                                             int32_t event_id, void* event_data);
static esp_err_t mqtt_client_manager_handle_connected(void);
static esp_err_t mqtt_client_manager_handle_disconnected(void);
static esp_err_t mqtt_client_manager_handle_data(esp_mqtt_event_handle_t event);
static esp_err_t mqtt_client_manager_handle_error(esp_mqtt_event_handle_t event);

/* Configuration management */
static esp_err_t mqtt_client_manager_validate_config(const mqtt_client_manager_config_t* config);
static esp_err_t mqtt_client_manager_apply_config(void);
static esp_err_t mqtt_client_manager_load_ssl_certificates(void);

/* Message handling */
static esp_err_t mqtt_client_manager_process_message_queue(void);
static esp_err_t mqtt_client_manager_add_to_queue(mqtt_client_manager_message_t* message);
static mqtt_client_manager_message_t* mqtt_client_manager_remove_from_queue(void);

/* Subscription management */
static esp_err_t mqtt_client_manager_add_subscription(const char* topic, mqtt_client_manager_qos_t qos);
static esp_err_t mqtt_client_manager_remove_subscription(const char* topic);
static mqtt_client_manager_subscription_t* mqtt_client_manager_find_subscription(const char* topic);

/* Event/callback management */
static esp_err_t mqtt_client_manager_fire_event(mqtt_client_manager_event_t event, void* data);
static esp_err_t mqtt_client_manager_fire_message_callbacks(const mqtt_client_manager_message_t* message);
static esp_err_t mqtt_client_manager_fire_topic_callbacks(const char* topic, const char* payload, size_t payload_len);

/* Utility functions */
static uint32_t mqtt_client_manager_get_timestamp_ms(void);
static int mqtt_client_manager_generate_message_id(void);
static esp_err_t mqtt_client_manager_update_statistics(void);
static esp_err_t mqtt_client_manager_check_reconnection(void);

/* JSON configuration helpers */
static esp_err_t mqtt_client_manager_config_to_json(const mqtt_client_manager_config_t* config, cJSON** json);
static esp_err_t mqtt_client_manager_config_from_json(const cJSON* json, mqtt_client_manager_config_t* config);

/* ============================= PUBLIC FUNCTIONS ============================= */

/* ============================= INITIALIZATION ============================= */

esp_err_t mqtt_client_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing MQTT Client Manager v%s", MQTT_CLIENT_MANAGER_VERSION);
    
    if (g_mqtt_context != NULL) {
        ESP_LOGW(TAG, "MQTT Client Manager already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = mqtt_client_manager_create_context();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create MQTT context: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Get default configuration */
    ret = mqtt_client_manager_get_default_config(&g_mqtt_context->config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get default configuration: %s", esp_err_to_name(ret));
        mqtt_client_manager_destroy_context();
        return ret;
    }
    
    /* Try to load saved configuration */
    ret = mqtt_client_manager_load_config();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Could not load saved configuration, using defaults");
    }
    
    /* Initialize status */
    g_mqtt_context->status.state = MQTT_CLIENT_MANAGER_STATE_INITIALIZED;
    g_mqtt_context->status.connected = false;
    strcpy(g_mqtt_context->status.status_message, "Initialized");
    
    /* Start internal task */
    BaseType_t task_ret = xTaskCreate(mqtt_client_manager_task, "mqtt_client_mgr", 
                                     MQTT_CLIENT_MANAGER_TASK_STACK_SIZE, NULL, 
                                     MQTT_CLIENT_MANAGER_TASK_PRIORITY, 
                                     &g_mqtt_context->task_handle);
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create MQTT client task");
        mqtt_client_manager_destroy_context();
        return ESP_ERR_NO_MEM;
    }
    
    g_mqtt_context->initialized = true;
    g_mqtt_context->running = true;
    
    ESP_LOGI(TAG, "MQTT Client Manager initialized successfully");
    return ESP_OK;
}

esp_err_t mqtt_client_manager_init_with_config(const mqtt_client_manager_config_t* config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Configuration cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = mqtt_client_manager_validate_config(config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Invalid configuration provided");
        return ret;
    }
    
    ret = mqtt_client_manager_init();
    if (ret != ESP_OK) {
        return ret;
    }
    
    ret = mqtt_client_manager_set_config(config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply provided configuration");
        mqtt_client_manager_deinit();
        return ret;
    }
    
    return ESP_OK;
}

esp_err_t mqtt_client_manager_deinit(void)
{
    if (g_mqtt_context == NULL) {
        ESP_LOGW(TAG, "MQTT Client Manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Deinitializing MQTT Client Manager");
    
    /* Disconnect if connected */
    if (g_mqtt_context->status.connected) {
        mqtt_client_manager_disconnect();
        
        /* Wait for disconnection */
        EventBits_t bits = xEventGroupWaitBits(g_mqtt_context->event_group,
                                              MQTT_DISCONNECTED_BIT,
                                              pdFALSE, pdFALSE,
                                              pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_MAX_WAIT_MS));
        
        if (!(bits & MQTT_DISCONNECTED_BIT)) {
            ESP_LOGW(TAG, "Disconnect timeout, forcing cleanup");
        }
    }
    
    /* Stop internal task */
    g_mqtt_context->running = false;
    if (g_mqtt_context->task_handle != NULL) {
        vTaskDelete(g_mqtt_context->task_handle);
    }
    
    /* Cleanup resources */
    esp_err_t ret = mqtt_client_manager_destroy_context();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Error during context cleanup: %s", esp_err_to_name(ret));
    }
    
    ESP_LOGI(TAG, "MQTT Client Manager deinitialized");
    return ESP_OK;
}

bool mqtt_client_manager_is_initialized(void)
{
    return (g_mqtt_context != NULL && g_mqtt_context->initialized);
}

/* ============================= CONFIGURATION ============================= */

esp_err_t mqtt_client_manager_get_default_config(mqtt_client_manager_config_t* config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(config, 0, sizeof(mqtt_client_manager_config_t));
    
    /* Broker configuration */
    strcpy(config->broker.broker_url, "mqtt://localhost");
    config->broker.port = MQTT_CLIENT_MANAGER_DEFAULT_PORT;
    config->broker.transport = MQTT_CLIENT_MANAGER_TRANSPORT_TCP;
    snprintf(config->broker.client_id, sizeof(config->broker.client_id), "esp32_%06x", 
             (unsigned int)(esp_random() & 0xFFFFFF));
    config->broker.keepalive = MQTT_CLIENT_MANAGER_DEFAULT_KEEPALIVE;
    config->broker.clean_session = true;
    config->broker.connect_timeout_ms = MQTT_CLIENT_MANAGER_DEFAULT_CONNECT_TIMEOUT;
    config->broker.network_timeout_ms = 5000;
    config->broker.buffer_size = 1024;
    config->broker.auto_reconnect = true;
    config->broker.reconnect_timeout_ms = MQTT_CLIENT_MANAGER_DEFAULT_RETRY_INTERVAL;
    config->broker.max_reconnect_interval_ms = 60000;
    config->broker.max_reconnect_attempts = 0; /* Infinite */
    
    /* SSL configuration */
    config->ssl.enabled = false;
    config->ssl.verify_peer = true;
    config->ssl.verify_hostname = true;
    config->ssl.use_global_ca_store = false;
    config->ssl.skip_cert_common_name_check = false;
    
    /* Will configuration */
    config->will.enabled = false;
    config->will.qos = MQTT_CLIENT_MANAGER_QOS_1;
    config->will.retain = false;
    
    /* General configuration */
    config->enabled = true;
    config->default_qos = MQTT_CLIENT_MANAGER_DEFAULT_QOS;
    config->default_retain = false;
    config->publish_timeout_ms = 5000;
    config->subscribe_timeout_ms = 5000;
    config->max_inflight_messages = 16;
    config->enable_message_queue = true;
    config->message_queue_size = MQTT_CLIENT_MANAGER_MAX_MESSAGE_QUEUE;
    config->persist_session = false;
    config->enable_metrics = true;
    
    return ESP_OK;
}

esp_err_t mqtt_client_manager_set_config(const mqtt_client_manager_config_t* config)
{
    if (!mqtt_client_manager_is_initialized() || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = mqtt_client_manager_validate_config(config);
    if (ret != ESP_OK) {
        return ret;
    }
    
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_MAX_WAIT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    /* Store new configuration */
    memcpy(&g_mqtt_context->config, config, sizeof(mqtt_client_manager_config_t));
    
    /* Apply configuration if connected */
    if (g_mqtt_context->status.connected) {
        ESP_LOGW(TAG, "Configuration changed while connected - reconnection required");
        g_mqtt_context->status.state = MQTT_CLIENT_MANAGER_STATE_DISCONNECTING;
    }
    
    xSemaphoreGive(g_mqtt_context->mutex);
    
    ESP_LOGI(TAG, "Configuration updated successfully");
    return ESP_OK;
}

esp_err_t mqtt_client_manager_get_config(mqtt_client_manager_config_t* config)
{
    if (!mqtt_client_manager_is_initialized() || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_MAX_WAIT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    memcpy(config, &g_mqtt_context->config, sizeof(mqtt_client_manager_config_t));
    
    xSemaphoreGive(g_mqtt_context->mutex);
    return ESP_OK;
}

esp_err_t mqtt_client_manager_load_config(void)
{
    if (!mqtt_client_manager_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Loading configuration from storage");
    
    FILE* file = fopen(MQTT_CLIENT_MANAGER_CONFIG_FILE, "r");
    if (file == NULL) {
        ESP_LOGW(TAG, "Configuration file not found");
        return ESP_ERR_NOT_FOUND;
    }
    
    /* Get file size */
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size <= 0 || file_size > 10240) {
        ESP_LOGE(TAG, "Invalid configuration file size: %ld", file_size);
        fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }
    
    /* Read file content */
    char* json_string = malloc(file_size + 1);
    if (json_string == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for configuration");
        fclose(file);
        return ESP_ERR_NO_MEM;
    }
    
    size_t bytes_read = fread(json_string, 1, file_size, file);
    fclose(file);
    
    if (bytes_read != file_size) {
        ESP_LOGE(TAG, "Failed to read configuration file");
        free(json_string);
        return ESP_FAIL;
    }
    
    json_string[file_size] = '\0';
    
    /* Parse JSON */
    cJSON* json = cJSON_Parse(json_string);
    free(json_string);
    
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to parse configuration JSON");
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Convert from JSON */
    mqtt_client_manager_config_t config;
    esp_err_t ret = mqtt_client_manager_config_from_json(json, &config);
    cJSON_Delete(json);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to convert configuration from JSON");
        return ret;
    }
    
    /* Apply configuration */
    ret = mqtt_client_manager_set_config(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply loaded configuration");
        return ret;
    }
    
    ESP_LOGI(TAG, "Configuration loaded successfully");
    return ESP_OK;
}

esp_err_t mqtt_client_manager_save_config(void)
{
    if (!mqtt_client_manager_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Saving configuration to storage");
    
    /* Convert configuration to JSON */
    cJSON* json = NULL;
    esp_err_t ret = mqtt_client_manager_config_to_json(&g_mqtt_context->config, &json);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to convert configuration to JSON");
        return ret;
    }
    
    /* Convert to string */
    char* json_string = cJSON_Print(json);
    cJSON_Delete(json);
    
    if (json_string == NULL) {
        ESP_LOGE(TAG, "Failed to generate JSON string");
        return ESP_ERR_NO_MEM;
    }
    
    /* Write to file */
    FILE* file = fopen(MQTT_CLIENT_MANAGER_CONFIG_FILE, "w");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open configuration file for writing");
        free(json_string);
        return ESP_FAIL;
    }
    
    size_t len = strlen(json_string);
    size_t written = fwrite(json_string, 1, len, file);
    fclose(file);
    free(json_string);
    
    if (written != len) {
        ESP_LOGE(TAG, "Failed to write complete configuration");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Configuration saved successfully");
    return ESP_OK;
}

/* ============================= CONNECTION MANAGEMENT ============================= */

esp_err_t mqtt_client_manager_connect(void)
{
    if (!mqtt_client_manager_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (g_mqtt_context->status.connected) {
        ESP_LOGW(TAG, "Already connected to MQTT broker");
        return ESP_OK;
    }
    
    if (g_mqtt_context->status.state == MQTT_CLIENT_MANAGER_STATE_CONNECTING) {
        ESP_LOGW(TAG, "Connection already in progress");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Connecting to MQTT broker: %s:%d", 
             g_mqtt_context->config.broker.broker_url, 
             g_mqtt_context->config.broker.port);
    
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_MAX_WAIT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    /* Update status */
    g_mqtt_context->status.state = MQTT_CLIENT_MANAGER_STATE_CONNECTING;
    g_mqtt_context->connection_start_time = mqtt_client_manager_get_timestamp_ms();
    strcpy(g_mqtt_context->status.status_message, "Connecting...");
    
    /* Clear event bits */
    xEventGroupClearBits(g_mqtt_context->event_group, 
                        MQTT_CONNECTED_BIT | MQTT_DISCONNECTED_BIT | MQTT_ERROR_BIT);
    
    /* Apply current configuration */
    esp_err_t ret = mqtt_client_manager_apply_config();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply configuration: %s", esp_err_to_name(ret));
        g_mqtt_context->status.state = MQTT_CLIENT_MANAGER_STATE_ERROR;
        strcpy(g_mqtt_context->status.status_message, "Configuration error");
        xSemaphoreGive(g_mqtt_context->mutex);
        return ret;
    }
    
    /* Start MQTT client */
    ret = esp_mqtt_client_start(g_mqtt_context->mqtt_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(ret));
        g_mqtt_context->status.state = MQTT_CLIENT_MANAGER_STATE_ERROR;
        strcpy(g_mqtt_context->status.status_message, "Failed to start client");
        xSemaphoreGive(g_mqtt_context->mutex);
        return ret;
    }
    
    xSemaphoreGive(g_mqtt_context->mutex);
    
    /* Fire before connect event */
    mqtt_client_manager_fire_event(MQTT_CLIENT_MANAGER_EVENT_BEFORE_CONNECT, NULL);
    
    return ESP_OK;
}

esp_err_t mqtt_client_manager_connect_with_config(const mqtt_client_manager_broker_config_t* broker_config)
{
    if (broker_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!mqtt_client_manager_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Update broker configuration */
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_MAX_WAIT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    memcpy(&g_mqtt_context->config.broker, broker_config, sizeof(mqtt_client_manager_broker_config_t));
    
    xSemaphoreGive(g_mqtt_context->mutex);
    
    return mqtt_client_manager_connect();
}

esp_err_t mqtt_client_manager_disconnect(void)
{
    if (!mqtt_client_manager_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!g_mqtt_context->status.connected) {
        ESP_LOGW(TAG, "Not connected to MQTT broker");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Disconnecting from MQTT broker");
    
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_MAX_WAIT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    g_mqtt_context->status.state = MQTT_CLIENT_MANAGER_STATE_DISCONNECTING;
    strcpy(g_mqtt_context->status.status_message, "Disconnecting...");
    
    esp_err_t ret = esp_mqtt_client_stop(g_mqtt_context->mqtt_client);
    
    xSemaphoreGive(g_mqtt_context->mutex);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop MQTT client: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

esp_err_t mqtt_client_manager_reconnect(void)
{
    if (!mqtt_client_manager_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Reconnecting to MQTT broker");
    
    /* Disconnect first if connected */
    if (g_mqtt_context->status.connected) {
        esp_err_t ret = mqtt_client_manager_disconnect();
        if (ret != ESP_OK) {
            return ret;
        }
        
        /* Wait for disconnection */
        EventBits_t bits = xEventGroupWaitBits(g_mqtt_context->event_group,
                                              MQTT_DISCONNECTED_BIT,
                                              pdFALSE, pdFALSE,
                                              pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_MAX_WAIT_MS));
        
        if (!(bits & MQTT_DISCONNECTED_BIT)) {
            ESP_LOGW(TAG, "Disconnect timeout during reconnection");
        }
    }
    
    return mqtt_client_manager_connect();
}

bool mqtt_client_manager_is_connected(void)
{
    if (!mqtt_client_manager_is_initialized()) {
        return false;
    }
    
    return g_mqtt_context->status.connected;
}

mqtt_client_manager_state_t mqtt_client_manager_get_state(void)
{
    if (!mqtt_client_manager_is_initialized()) {
        return MQTT_CLIENT_MANAGER_STATE_UNINITIALIZED;
    }
    
    return g_mqtt_context->status.state;
}

/* ============================= PRIVATE FUNCTIONS ============================= */

static esp_err_t mqtt_client_manager_create_context(void)
{
    g_mqtt_context = heap_caps_calloc(1, sizeof(mqtt_client_manager_context_t), MALLOC_CAP_8BIT);
    if (g_mqtt_context == NULL) {
        ESP_LOGE(TAG, "Failed to allocate context memory");
        return ESP_ERR_NO_MEM;
    }
    
    /* Create synchronization objects */
    g_mqtt_context->mutex = xSemaphoreCreateMutex();
    if (g_mqtt_context->mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        goto error_cleanup;
    }
    
    g_mqtt_context->event_group = xEventGroupCreate();
    if (g_mqtt_context->event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        goto error_cleanup;
    }
    
    /* Create message queue */
    g_mqtt_context->message_queue = xQueueCreate(MQTT_CLIENT_MANAGER_QUEUE_SIZE, 
                                                 sizeof(mqtt_client_manager_queue_item_t));
    if (g_mqtt_context->message_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create message queue");
        goto error_cleanup;
    }
    
    /* Create internal queue */
    g_mqtt_context->internal_queue = xQueueCreate(32, sizeof(uint32_t));
    if (g_mqtt_context->internal_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create internal queue");
        goto error_cleanup;
    }
    
    /* Initialize statistics timer */
    const esp_timer_create_args_t timer_args = {
        .callback = mqtt_client_manager_stats_timer_callback,
        .arg = g_mqtt_context,
        .name = "mqtt_stats_timer"
    };
    
    esp_err_t ret = esp_timer_create(&timer_args, &g_mqtt_context->stats_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create statistics timer: %s", esp_err_to_name(ret));
        goto error_cleanup;
    }
    
    /* Initialize other fields */
    g_mqtt_context->next_message_id = 1;
    g_mqtt_context->subscription_count = 0;
    
    return ESP_OK;
    
error_cleanup:
    mqtt_client_manager_destroy_context();
    return ESP_ERR_NO_MEM;
}

static esp_err_t mqtt_client_manager_destroy_context(void)
{
    if (g_mqtt_context == NULL) {
        return ESP_OK;
    }
    
    /* Stop and delete timer */
    if (g_mqtt_context->stats_timer != NULL) {
        esp_timer_stop(g_mqtt_context->stats_timer);
        esp_timer_delete(g_mqtt_context->stats_timer);
    }
    
    /* Destroy MQTT client */
    if (g_mqtt_context->mqtt_client != NULL) {
        esp_mqtt_client_destroy(g_mqtt_context->mqtt_client);
    }
    
    /* Clean up SSL resources */
    if (g_mqtt_context->ca_cert_buffer != NULL) {
        free(g_mqtt_context->ca_cert_buffer);
    }
    if (g_mqtt_context->client_cert_buffer != NULL) {
        free(g_mqtt_context->client_cert_buffer);
    }
    if (g_mqtt_context->client_key_buffer != NULL) {
        free(g_mqtt_context->client_key_buffer);
    }
    
    /* Clean up callback lists */
    mqtt_client_manager_event_callback_node_t* event_node = g_mqtt_context->event_callbacks;
    while (event_node != NULL) {
        mqtt_client_manager_event_callback_node_t* next = event_node->next;
        free(event_node);
        event_node = next;
    }
    
    mqtt_client_manager_message_callback_node_t* msg_node = g_mqtt_context->message_callbacks;
    while (msg_node != NULL) {
        mqtt_client_manager_message_callback_node_t* next = msg_node->next;
        free(msg_node);
        msg_node = next;
    }
    
    mqtt_client_manager_topic_callback_node_t* topic_node = g_mqtt_context->topic_callbacks;
    while (topic_node != NULL) {
        mqtt_client_manager_topic_callback_node_t* next = topic_node->next;
        free(topic_node);
        topic_node = next;
    }
    
    /* Delete synchronization objects */
    if (g_mqtt_context->mutex != NULL) {
        vSemaphoreDelete(g_mqtt_context->mutex);
    }
    if (g_mqtt_context->event_group != NULL) {
        vEventGroupDelete(g_mqtt_context->event_group);
    }
    if (g_mqtt_context->message_queue != NULL) {
        vQueueDelete(g_mqtt_context->message_queue);
    }
    if (g_mqtt_context->internal_queue != NULL) {
        vQueueDelete(g_mqtt_context->internal_queue);
    }
    
    /* Free context */
    free(g_mqtt_context);
    g_mqtt_context = NULL;
    
    return ESP_OK;
}

static void mqtt_client_manager_task(void* pvParameters)
{
    ESP_LOGI(TAG, "MQTT Client Manager task started");
    
    TickType_t last_stats_update = xTaskGetTickCount();
    TickType_t last_reconnect_check = xTaskGetTickCount();
    
    while (g_mqtt_context->running) {
        TickType_t current_time = xTaskGetTickCount();
        
        /* Process message queue */
        mqtt_client_manager_process_message_queue();
        
        /* Update statistics periodically */
        if (current_time - last_stats_update >= pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_STATS_UPDATE_MS)) {
            mqtt_client_manager_update_statistics();
            last_stats_update = current_time;
        }
        
        /* Check reconnection periodically */
        if (current_time - last_reconnect_check >= pdMS_TO_TICKS(5000)) {
            mqtt_client_manager_check_reconnection();
            last_reconnect_check = current_time;
        }
        
        vTaskDelay(pdMS_TO_TICKS(100)); /* 100ms task loop */
    }
    
    ESP_LOGI(TAG, "MQTT Client Manager task stopping");
    vTaskDelete(NULL);
}

static void mqtt_client_manager_stats_timer_callback(void* arg)
{
    if (g_mqtt_context != NULL && g_mqtt_context->status.connected) {
        g_mqtt_context->status.stats.uptime_ms += MQTT_CLIENT_MANAGER_STATS_UPDATE_MS;
        g_mqtt_context->status.stats.total_uptime_ms += MQTT_CLIENT_MANAGER_STATS_UPDATE_MS;
    }
}

/* ============================= MQTT EVENT HANDLING ============================= */

static void mqtt_client_manager_event_handler(void* handler_args, esp_event_base_t base, 
                                             int32_t event_id, void* event_data)
{
    ESP_LOGD(TAG, "MQTT event dispatch: base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            mqtt_client_manager_handle_connected();
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            mqtt_client_manager_handle_disconnected();
            break;
            
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            g_mqtt_context->status.stats.messages_published++;
            break;
            
        case MQTT_EVENT_DATA:
            mqtt_client_manager_handle_data(event);
            break;
            
        case MQTT_EVENT_ERROR:
            mqtt_client_manager_handle_error(event);
            break;
            
        default:
            ESP_LOGD(TAG, "Unhandled MQTT event: %d", event_id);
            break;
    }
}

static esp_err_t mqtt_client_manager_handle_connected(void)
{
    ESP_LOGI(TAG, "Connected to MQTT broker");
    
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_MAX_WAIT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    /* Update status */
    g_mqtt_context->status.state = MQTT_CLIENT_MANAGER_STATE_CONNECTED;
    g_mqtt_context->status.connected = true;
    g_mqtt_context->status.connection_time_ms = mqtt_client_manager_get_timestamp_ms() - g_mqtt_context->connection_start_time;
    g_mqtt_context->last_activity_time = mqtt_client_manager_get_timestamp_ms();
    g_mqtt_context->reconnect_attempts = 0;
    
    /* Copy broker info to status */
    strcpy(g_mqtt_context->status.broker_url, g_mqtt_context->config.broker.broker_url);
    g_mqtt_context->status.broker_port = g_mqtt_context->config.broker.port;
    strcpy(g_mqtt_context->status.client_id, g_mqtt_context->config.broker.client_id);
    g_mqtt_context->status.transport = g_mqtt_context->config.broker.transport;
    
    strcpy(g_mqtt_context->status.status_message, "Connected");
    
    /* Update statistics */
    g_mqtt_context->status.stats.connection_attempts++;
    g_mqtt_context->status.stats.successful_connections++;
    
    xSemaphoreGive(g_mqtt_context->mutex);
    
    /* Set event bit */
    xEventGroupSetBits(g_mqtt_context->event_group, MQTT_CONNECTED_BIT);
    
    /* Start statistics timer */
    if (g_mqtt_context->config.enable_metrics) {
        esp_timer_start_periodic(g_mqtt_context->stats_timer, MQTT_CLIENT_MANAGER_STATS_UPDATE_MS * 1000);
    }
    
    /* Fire connected event */
    mqtt_client_manager_fire_event(MQTT_CLIENT_MANAGER_EVENT_CONNECTED, NULL);
    
    return ESP_OK;
}

static esp_err_t mqtt_client_manager_handle_disconnected(void)
{
    ESP_LOGI(TAG, "Disconnected from MQTT broker");
    
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_MAX_WAIT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    bool was_connected = g_mqtt_context->status.connected;
    
    /* Update status */
    g_mqtt_context->status.state = MQTT_CLIENT_MANAGER_STATE_DISCONNECTED;
    g_mqtt_context->status.connected = false;
    strcpy(g_mqtt_context->status.status_message, "Disconnected");
    
    /* Update statistics */
    if (was_connected) {
        g_mqtt_context->status.stats.disconnections++;
    }
    
    xSemaphoreGive(g_mqtt_context->mutex);
    
    /* Stop statistics timer */
    esp_timer_stop(g_mqtt_context->stats_timer);
    
    /* Set event bit */
    xEventGroupSetBits(g_mqtt_context->event_group, MQTT_DISCONNECTED_BIT);
    
    /* Fire appropriate event */
    if (was_connected && g_mqtt_context->config.broker.auto_reconnect) {
        mqtt_client_manager_fire_event(MQTT_CLIENT_MANAGER_EVENT_CONNECTION_LOST, NULL);
        
        /* Schedule reconnection */
        g_mqtt_context->next_reconnect_time = mqtt_client_manager_get_timestamp_ms() + 
                                             g_mqtt_context->config.broker.reconnect_timeout_ms;
        g_mqtt_context->status.state = MQTT_CLIENT_MANAGER_STATE_RECONNECTING;
    } else {
        mqtt_client_manager_fire_event(MQTT_CLIENT_MANAGER_EVENT_DISCONNECTED, NULL);
    }
    
    return ESP_OK;
}

static esp_err_t mqtt_client_manager_handle_data(esp_mqtt_event_handle_t event)
{
    if (event->topic_len == 0 || event->data_len == 0) {
        ESP_LOGW(TAG, "Received empty message");
        return ESP_OK;
    }
    
    /* Create null-terminated strings */
    char* topic = malloc(event->topic_len + 1);
    char* payload = malloc(event->data_len + 1);
    
    if (topic == NULL || payload == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for message");
        if (topic) free(topic);
        if (payload) free(payload);
        return ESP_ERR_NO_MEM;
    }
    
    memcpy(topic, event->topic, event->topic_len);
    topic[event->topic_len] = '\0';
    
    memcpy(payload, event->data, event->data_len);
    payload[event->data_len] = '\0';
    
    ESP_LOGD(TAG, "Received message on topic '%s': %.*s", topic, event->data_len, payload);
    
    /* Update statistics */
    g_mqtt_context->status.stats.messages_received++;
    g_mqtt_context->status.stats.bytes_received += event->data_len;
    g_mqtt_context->last_activity_time = mqtt_client_manager_get_timestamp_ms();
    
    /* Update subscription statistics */
    mqtt_client_manager_subscription_t* subscription = mqtt_client_manager_find_subscription(topic);
    if (subscription != NULL) {
        subscription->message_count++;
        subscription->last_message_time = mqtt_client_manager_get_timestamp_ms();
    }
    
    /* Create message structure */
    mqtt_client_manager_message_t message;
    memset(&message, 0, sizeof(message));
    
    strncpy(message.topic, topic, sizeof(message.topic) - 1);
    message.payload = payload;
    message.payload_len = event->data_len;
    message.qos = MQTT_CLIENT_MANAGER_QOS_0; /* Will be updated based on subscription */
    message.retain = false; /* ESP-MQTT doesn't provide retain flag in event */
    message.timestamp_ms = mqtt_client_manager_get_timestamp_ms();
    message.message_id = event->msg_id;
    message.delivery_status = MQTT_CLIENT_MANAGER_DELIVERY_DELIVERED;
    
    /* Fire callbacks */
    mqtt_client_manager_fire_message_callbacks(&message);
    mqtt_client_manager_fire_topic_callbacks(topic, payload, event->data_len);
    
    /* Fire general message received event */
    mqtt_client_manager_fire_event(MQTT_CLIENT_MANAGER_EVENT_MESSAGE_RECEIVED, &message);
    
    free(topic);
    free(payload);
    
    return ESP_OK;
}

static esp_err_t mqtt_client_manager_handle_error(esp_mqtt_event_handle_t event)
{
    ESP_LOGE(TAG, "MQTT error occurred");
    
    if (event->error_handle->error_type != MQTT_ERROR_TYPE_NONE) {
        ESP_LOGE(TAG, "MQTT error type: %d", event->error_handle->error_type);
    }
    
    /* Update status */
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        g_mqtt_context->status.state = MQTT_CLIENT_MANAGER_STATE_ERROR;
        strcpy(g_mqtt_context->status.status_message, "Error occurred");
        g_mqtt_context->status.stats.messages_failed++;
        xSemaphoreGive(g_mqtt_context->mutex);
    }
    
    /* Set error bit */
    xEventGroupSetBits(g_mqtt_context->event_group, MQTT_ERROR_BIT);
    
    /* Fire error event */
    mqtt_client_manager_fire_event(MQTT_CLIENT_MANAGER_EVENT_ERROR, event);
    
    return ESP_OK;
}

/* ============================= CONFIGURATION HELPERS ============================= */

static esp_err_t mqtt_client_manager_validate_config(const mqtt_client_manager_config_t* config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Validate broker URL */
    if (strlen(config->broker.broker_url) == 0) {
        ESP_LOGE(TAG, "Broker URL cannot be empty");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!mqtt_client_manager_validate_broker_url(config->broker.broker_url)) {
        ESP_LOGE(TAG, "Invalid broker URL format");
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Validate port */
    if (config->broker.port == 0) {
        ESP_LOGE(TAG, "Invalid broker port");
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Validate client ID */
    if (strlen(config->broker.client_id) == 0) {
        ESP_LOGE(TAG, "Client ID cannot be empty");
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Validate timeouts */
    if (config->broker.connect_timeout_ms == 0 || config->broker.connect_timeout_ms > 60000) {
        ESP_LOGE(TAG, "Invalid connection timeout");
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Validate SSL configuration if enabled */
    if (config->ssl.enabled) {
        if (config->broker.transport != MQTT_CLIENT_MANAGER_TRANSPORT_SSL &&
            config->broker.transport != MQTT_CLIENT_MANAGER_TRANSPORT_WSS) {
            ESP_LOGE(TAG, "SSL enabled but transport is not SSL/WSS");
            return ESP_ERR_INVALID_ARG;
        }
        
        if (config->ssl.verify_peer && 
            strlen(config->ssl.ca_cert_path) == 0 && 
            config->ssl.ca_cert_pem == NULL &&
            !config->ssl.use_global_ca_store) {
            ESP_LOGE(TAG, "SSL peer verification enabled but no CA certificate provided");
            return ESP_ERR_INVALID_ARG;
        }
    }
    
    /* Validate will configuration if enabled */
    if (config->will.enabled) {
        if (strlen(config->will.topic) == 0) {
            ESP_LOGE(TAG, "Will topic cannot be empty when will is enabled");
            return ESP_ERR_INVALID_ARG;
        }
        
        if (!mqtt_client_manager_validate_topic(config->will.topic, false)) {
            ESP_LOGE(TAG, "Invalid will topic format");
            return ESP_ERR_INVALID_ARG;
        }
    }
    
    return ESP_OK;
}

static esp_err_t mqtt_client_manager_apply_config(void)
{
    /* Create MQTT client configuration */
    esp_mqtt_client_config_t mqtt_cfg = {0};
    
    /* Basic configuration */
    mqtt_cfg.uri = g_mqtt_context->config.broker.broker_url;
    mqtt_cfg.port = g_mqtt_context->config.broker.port;
    mqtt_cfg.client_id = g_mqtt_context->config.broker.client_id;
    mqtt_cfg.username = strlen(g_mqtt_context->config.broker.username) > 0 ? 
                        g_mqtt_context->config.broker.username : NULL;
    mqtt_cfg.password = strlen(g_mqtt_context->config.broker.password) > 0 ? 
                        g_mqtt_context->config.broker.password : NULL;
    
    /* Connection parameters */
    mqtt_cfg.keepalive = g_mqtt_context->config.broker.keepalive;
    mqtt_cfg.disable_clean_session = !g_mqtt_context->config.broker.clean_session;
    mqtt_cfg.network_timeout_ms = g_mqtt_context->config.broker.network_timeout_ms;
    mqtt_cfg.buffer_size = g_mqtt_context->config.broker.buffer_size;
    
    /* Auto-reconnect */
    mqtt_cfg.disable_auto_reconnect = !g_mqtt_context->config.broker.auto_reconnect;
    mqtt_cfg.reconnect_timeout_ms = g_mqtt_context->config.broker.reconnect_timeout_ms;
    
    /* Transport type */
    switch (g_mqtt_context->config.broker.transport) {
        case MQTT_CLIENT_MANAGER_TRANSPORT_TCP:
            mqtt_cfg.transport = MQTT_TRANSPORT_OVER_TCP;
            break;
        case MQTT_CLIENT_MANAGER_TRANSPORT_SSL:
            mqtt_cfg.transport = MQTT_TRANSPORT_OVER_SSL;
            break;
        case MQTT_CLIENT_MANAGER_TRANSPORT_WS:
            mqtt_cfg.transport = MQTT_TRANSPORT_OVER_WS;
            break;
        case MQTT_CLIENT_MANAGER_TRANSPORT_WSS:
            mqtt_cfg.transport = MQTT_TRANSPORT_OVER_WSS;
            break;
    }
    
    /* SSL/TLS configuration */
    if (g_mqtt_context->config.ssl.enabled) {
        if (strlen(g_mqtt_context->config.ssl.ca_cert_path) > 0) {
            esp_err_t ret = mqtt_client_manager_load_ssl_certificates();
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to load SSL certificates");
                return ret;
            }
            mqtt_cfg.cert_pem = g_mqtt_context->ca_cert_buffer;
        } else if (g_mqtt_context->config.ssl.ca_cert_pem != NULL) {
            mqtt_cfg.cert_pem = g_mqtt_context->config.ssl.ca_cert_pem;
        }
        
        if (g_mqtt_context->config.ssl.client_cert_pem != NULL) {
            mqtt_cfg.client_cert_pem = g_mqtt_context->config.ssl.client_cert_pem;
        }
        
        if (g_mqtt_context->config.ssl.client_key_pem != NULL) {
            mqtt_cfg.client_key_pem = g_mqtt_context->config.ssl.client_key_pem;
        }
        
        mqtt_cfg.skip_cert_common_name_check = g_mqtt_context->config.ssl.skip_cert_common_name_check;
        mqtt_cfg.use_global_ca_store = g_mqtt_context->config.ssl.use_global_ca_store;
    }
    
    /* Last Will and Testament */
    if (g_mqtt_context->config.will.enabled) {
        mqtt_cfg.lwt_topic = g_mqtt_context->config.will.topic;
        mqtt_cfg.lwt_msg = g_mqtt_context->config.will.message;
        mqtt_cfg.lwt_msg_len = g_mqtt_context->config.will.message_len;
        mqtt_cfg.lwt_qos = g_mqtt_context->config.will.qos;
        mqtt_cfg.lwt_retain = g_mqtt_context->config.will.retain;
    }
    
    /* Destroy existing client if any */
    if (g_mqtt_context->mqtt_client != NULL) {
        esp_mqtt_client_destroy(g_mqtt_context->mqtt_client);
    }
    
    /* Create new client */
    g_mqtt_context->mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (g_mqtt_context->mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }
    
    /* Register event handler */
    esp_err_t ret = esp_mqtt_client_register_event(g_mqtt_context->mqtt_client, 
                                                   ESP_EVENT_ANY_ID, 
                                                   mqtt_client_manager_event_handler, 
                                                   NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler");
        esp_mqtt_client_destroy(g_mqtt_context->mqtt_client);
        g_mqtt_context->mqtt_client = NULL;
        return ret;
    }
    
    return ESP_OK;
}

static esp_err_t mqtt_client_manager_load_ssl_certificates(void)
{
    /* Load CA certificate */
    if (strlen(g_mqtt_context->config.ssl.ca_cert_path) > 0) {
        FILE* ca_file = fopen(g_mqtt_context->config.ssl.ca_cert_path, "r");
        if (ca_file == NULL) {
            ESP_LOGE(TAG, "Failed to open CA certificate file: %s", 
                     g_mqtt_context->config.ssl.ca_cert_path);
            return ESP_ERR_NOT_FOUND;
        }
        
        /* Get file size */
        fseek(ca_file, 0, SEEK_END);
        long ca_size = ftell(ca_file);
        fseek(ca_file, 0, SEEK_SET);
        
        if (ca_size <= 0) {
            ESP_LOGE(TAG, "Invalid CA certificate file size");
            fclose(ca_file);
            return ESP_ERR_INVALID_SIZE;
        }
        
        /* Allocate buffer */
        if (g_mqtt_context->ca_cert_buffer != NULL) {
            free(g_mqtt_context->ca_cert_buffer);
        }
        
        g_mqtt_context->ca_cert_buffer = malloc(ca_size + 1);
        if (g_mqtt_context->ca_cert_buffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate CA certificate buffer");
            fclose(ca_file);
            return ESP_ERR_NO_MEM;
        }
        
        /* Read certificate */
        size_t bytes_read = fread(g_mqtt_context->ca_cert_buffer, 1, ca_size, ca_file);
        fclose(ca_file);
        
        if (bytes_read != ca_size) {
            ESP_LOGE(TAG, "Failed to read CA certificate file");
            free(g_mqtt_context->ca_cert_buffer);
            g_mqtt_context->ca_cert_buffer = NULL;
            return ESP_FAIL;
        }
        
        g_mqtt_context->ca_cert_buffer[ca_size] = '\0';
        ESP_LOGI(TAG, "Loaded CA certificate from %s (%ld bytes)", 
                 g_mqtt_context->config.ssl.ca_cert_path, ca_size);
    }
    
    return ESP_OK;
}

/* ============================= UTILITY FUNCTIONS ============================= */

static uint32_t mqtt_client_manager_get_timestamp_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static int mqtt_client_manager_generate_message_id(void)
{
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return -1;
    }
    
    int id = g_mqtt_context->next_message_id++;
    if (g_mqtt_context->next_message_id > 65535) {
        g_mqtt_context->next_message_id = 1;
    }
    
    xSemaphoreGive(g_mqtt_context->mutex);
    return id;
}

static esp_err_t mqtt_client_manager_update_statistics(void)
{
    if (g_mqtt_context->status.connected) {
        g_mqtt_context->last_activity_time = mqtt_client_manager_get_timestamp_ms();
    }
    return ESP_OK;
}

/* ============================= JSON CONFIGURATION HELPERS ============================= */

static esp_err_t mqtt_client_manager_config_to_json(const mqtt_client_manager_config_t* config, cJSON** json)
{
    *json = cJSON_CreateObject();
    if (*json == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    /* Broker configuration */
    cJSON* broker = cJSON_CreateObject();
    cJSON_AddItemToObject(*json, "broker", broker);
    cJSON_AddStringToObject(broker, "url", config->broker.broker_url);
    cJSON_AddNumberToObject(broker, "port", config->broker.port);
    cJSON_AddNumberToObject(broker, "transport", config->broker.transport);
    cJSON_AddStringToObject(broker, "client_id", config->broker.client_id);
    cJSON_AddStringToObject(broker, "username", config->broker.username);
    /* Note: Password not saved for security */
    cJSON_AddNumberToObject(broker, "keepalive", config->broker.keepalive);
    cJSON_AddBoolToObject(broker, "clean_session", config->broker.clean_session);
    cJSON_AddBoolToObject(broker, "auto_reconnect", config->broker.auto_reconnect);
    
    /* SSL configuration */
    cJSON* ssl = cJSON_CreateObject();
    cJSON_AddItemToObject(*json, "ssl", ssl);
    cJSON_AddBoolToObject(ssl, "enabled", config->ssl.enabled);
    cJSON_AddStringToObject(ssl, "ca_cert_path", config->ssl.ca_cert_path);
    cJSON_AddBoolToObject(ssl, "verify_peer", config->ssl.verify_peer);
    cJSON_AddBoolToObject(ssl, "verify_hostname", config->ssl.verify_hostname);
    
    /* General configuration */
    cJSON_AddBoolToObject(*json, "enabled", config->enabled);
    cJSON_AddNumberToObject(*json, "default_qos", config->default_qos);
    cJSON_AddBoolToObject(*json, "default_retain", config->default_retain);
    cJSON_AddBoolToObject(*json, "enable_message_queue", config->enable_message_queue);
    cJSON_AddNumberToObject(*json, "message_queue_size", config->message_queue_size);
    
    return ESP_OK;
}

static esp_err_t mqtt_client_manager_config_from_json(const cJSON* json, mqtt_client_manager_config_t* config)
{
    /* Get default configuration first */
    esp_err_t ret = mqtt_client_manager_get_default_config(config);
    if (ret != ESP_OK) {
        return ret;
    }
    
    /* Parse broker configuration */
    cJSON* broker = cJSON_GetObjectItem(json, "broker");
    if (broker != NULL) {
        cJSON* url = cJSON_GetObjectItem(broker, "url");
        if (cJSON_IsString(url)) {
            strncpy(config->broker.broker_url, url->valuestring, sizeof(config->broker.broker_url) - 1);
        }
        
        cJSON* port = cJSON_GetObjectItem(broker, "port");
        if (cJSON_IsNumber(port)) {
            config->broker.port = port->valueint;
        }
        
        cJSON* transport = cJSON_GetObjectItem(broker, "transport");
        if (cJSON_IsNumber(transport)) {
            config->broker.transport = transport->valueint;
        }
        
        cJSON* client_id = cJSON_GetObjectItem(broker, "client_id");
        if (cJSON_IsString(client_id)) {
            strncpy(config->broker.client_id, client_id->valuestring, sizeof(config->broker.client_id) - 1);
        }
        
        cJSON* username = cJSON_GetObjectItem(broker, "username");
        if (cJSON_IsString(username)) {
            strncpy(config->broker.username, username->valuestring, sizeof(config->broker.username) - 1);
        }
        
        cJSON* keepalive = cJSON_GetObjectItem(broker, "keepalive");
        if (cJSON_IsNumber(keepalive)) {
            config->broker.keepalive = keepalive->valueint;
        }
        
        cJSON* clean_session = cJSON_GetObjectItem(broker, "clean_session");
        if (cJSON_IsBool(clean_session)) {
            config->broker.clean_session = cJSON_IsTrue(clean_session);
        }
        
        cJSON* auto_reconnect = cJSON_GetObjectItem(broker, "auto_reconnect");
        if (cJSON_IsBool(auto_reconnect)) {
            config->broker.auto_reconnect = cJSON_IsTrue(auto_reconnect);
        }
    }
    
    /* Parse SSL configuration */
    cJSON* ssl = cJSON_GetObjectItem(json, "ssl");
    if (ssl != NULL) {
        cJSON* enabled = cJSON_GetObjectItem(ssl, "enabled");
        if (cJSON_IsBool(enabled)) {
            config->ssl.enabled = cJSON_IsTrue(enabled);
        }
        
        cJSON* ca_cert_path = cJSON_GetObjectItem(ssl, "ca_cert_path");
        if (cJSON_IsString(ca_cert_path)) {
            strncpy(config->ssl.ca_cert_path, ca_cert_path->valuestring, sizeof(config->ssl.ca_cert_path) - 1);
        }
        
        cJSON* verify_peer = cJSON_GetObjectItem(ssl, "verify_peer");
        if (cJSON_IsBool(verify_peer)) {
            config->ssl.verify_peer = cJSON_IsTrue(verify_peer);
        }
        
        cJSON* verify_hostname = cJSON_GetObjectItem(ssl, "verify_hostname");
        if (cJSON_IsBool(verify_hostname)) {
            config->ssl.verify_hostname = cJSON_IsTrue(verify_hostname);
        }
    }
    
    /* Parse general configuration */
    cJSON* enabled = cJSON_GetObjectItem(json, "enabled");
    if (cJSON_IsBool(enabled)) {
        config->enabled = cJSON_IsTrue(enabled);
    }
    
    cJSON* default_qos = cJSON_GetObjectItem(json, "default_qos");
    if (cJSON_IsNumber(default_qos)) {
        config->default_qos = default_qos->valueint;
    }
    
    cJSON* default_retain = cJSON_GetObjectItem(json, "default_retain");
    if (cJSON_IsBool(default_retain)) {
        config->default_retain = cJSON_IsTrue(default_retain);
    }
    
    cJSON* enable_message_queue = cJSON_GetObjectItem(json, "enable_message_queue");
    if (cJSON_IsBool(enable_message_queue)) {
        config->enable_message_queue = cJSON_IsTrue(enable_message_queue);
    }
    
    cJSON* message_queue_size = cJSON_GetObjectItem(json, "message_queue_size");
    if (cJSON_IsNumber(message_queue_size)) {
        config->message_queue_size = message_queue_size->valueint;
    }
    
    return ESP_OK;
}

/* ============================= CONTINUE WITH REMAINING FUNCTIONS ============================= */

/* Implementation continues with publish/subscribe operations, 
   event handling, and remaining utility functions... */