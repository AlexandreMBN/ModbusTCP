/**
 * @file mqtt_client_manager_utils.c
 * @brief MQTT Client Manager Utility Functions Implementation
 * 
 * This file contains utility functions, status management, and 
 * remaining API implementations for the MQTT Client Manager.
 * 
 * @version 1.0.0
 * @date 2024-11-10
 * @author ESP32 Development Team
 */

#include "mqtt_client_manager.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#define TAG "MQTT_CLIENT_UTILS"

/* External reference to the main context */
extern mqtt_client_manager_context_t* g_mqtt_context;

/* ============================= STATUS AND MONITORING ============================= */

esp_err_t mqtt_client_manager_get_status(mqtt_client_manager_status_t* status)
{
    if (!mqtt_client_manager_is_initialized() || status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_MAX_WAIT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    memcpy(status, &g_mqtt_context->status, sizeof(mqtt_client_manager_status_t));
    
    xSemaphoreGive(g_mqtt_context->mutex);
    return ESP_OK;
}

esp_err_t mqtt_client_manager_get_statistics(mqtt_client_manager_stats_t* stats)
{
    if (!mqtt_client_manager_is_initialized() || stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_MAX_WAIT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    memcpy(stats, &g_mqtt_context->status.stats, sizeof(mqtt_client_manager_stats_t));
    
    xSemaphoreGive(g_mqtt_context->mutex);
    return ESP_OK;
}

esp_err_t mqtt_client_manager_reset_statistics(void)
{
    if (!mqtt_client_manager_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_MAX_WAIT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    memset(&g_mqtt_context->status.stats, 0, sizeof(mqtt_client_manager_stats_t));
    g_mqtt_context->status.stats.subscription_count = g_mqtt_context->subscription_count;
    
    xSemaphoreGive(g_mqtt_context->mutex);
    
    ESP_LOGI(TAG, "Statistics reset");
    return ESP_OK;
}

esp_err_t mqtt_client_manager_set_status_message(const char* message)
{
    if (!mqtt_client_manager_is_initialized() || message == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_MAX_WAIT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    strncpy(g_mqtt_context->status.status_message, message, sizeof(g_mqtt_context->status.status_message) - 1);
    g_mqtt_context->status.status_message[sizeof(g_mqtt_context->status.status_message) - 1] = '\0';
    
    xSemaphoreGive(g_mqtt_context->mutex);
    return ESP_OK;
}

uint32_t mqtt_client_manager_get_uptime_ms(void)
{
    if (!mqtt_client_manager_is_initialized()) {
        return 0;
    }
    
    return g_mqtt_context->status.stats.uptime_ms;
}

/* ============================= EVENT HANDLING ============================= */

esp_err_t mqtt_client_manager_register_event_callback(mqtt_client_manager_event_callback_t callback, void* user_data)
{
    if (!mqtt_client_manager_is_initialized() || callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    mqtt_client_manager_event_callback_node_t* node = malloc(sizeof(mqtt_client_manager_event_callback_node_t));
    if (node == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for event callback");
        return ESP_ERR_NO_MEM;
    }
    
    node->callback = callback;
    node->user_data = user_data;
    
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_MAX_WAIT_MS)) != pdTRUE) {
        free(node);
        return ESP_ERR_TIMEOUT;
    }
    
    node->next = g_mqtt_context->event_callbacks;
    g_mqtt_context->event_callbacks = node;
    
    xSemaphoreGive(g_mqtt_context->mutex);
    
    ESP_LOGD(TAG, "Event callback registered");
    return ESP_OK;
}

esp_err_t mqtt_client_manager_register_message_callback(mqtt_client_manager_message_callback_t callback, void* user_data)
{
    if (!mqtt_client_manager_is_initialized() || callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    mqtt_client_manager_message_callback_node_t* node = malloc(sizeof(mqtt_client_manager_message_callback_node_t));
    if (node == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for message callback");
        return ESP_ERR_NO_MEM;
    }
    
    node->callback = callback;
    node->user_data = user_data;
    
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_MAX_WAIT_MS)) != pdTRUE) {
        free(node);
        return ESP_ERR_TIMEOUT;
    }
    
    node->next = g_mqtt_context->message_callbacks;
    g_mqtt_context->message_callbacks = node;
    
    xSemaphoreGive(g_mqtt_context->mutex);
    
    ESP_LOGD(TAG, "Message callback registered");
    return ESP_OK;
}

esp_err_t mqtt_client_manager_unregister_event_callback(mqtt_client_manager_event_callback_t callback)
{
    if (!mqtt_client_manager_is_initialized() || callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_MAX_WAIT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    mqtt_client_manager_event_callback_node_t** current = &g_mqtt_context->event_callbacks;
    while (*current != NULL) {
        if ((*current)->callback == callback) {
            mqtt_client_manager_event_callback_node_t* to_remove = *current;
            *current = (*current)->next;
            free(to_remove);
            xSemaphoreGive(g_mqtt_context->mutex);
            ESP_LOGD(TAG, "Event callback unregistered");
            return ESP_OK;
        }
        current = &(*current)->next;
    }
    
    xSemaphoreGive(g_mqtt_context->mutex);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t mqtt_client_manager_unregister_message_callback(mqtt_client_manager_message_callback_t callback)
{
    if (!mqtt_client_manager_is_initialized() || callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_MAX_WAIT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    mqtt_client_manager_message_callback_node_t** current = &g_mqtt_context->message_callbacks;
    while (*current != NULL) {
        if ((*current)->callback == callback) {
            mqtt_client_manager_message_callback_node_t* to_remove = *current;
            *current = (*current)->next;
            free(to_remove);
            xSemaphoreGive(g_mqtt_context->mutex);
            ESP_LOGD(TAG, "Message callback unregistered");
            return ESP_OK;
        }
        current = &(*current)->next;
    }
    
    xSemaphoreGive(g_mqtt_context->mutex);
    return ESP_ERR_NOT_FOUND;
}

/* ============================= ADVANCED FEATURES ============================= */

esp_err_t mqtt_client_manager_set_auto_reconnect(bool enable, uint32_t retry_interval_ms, uint8_t max_attempts)
{
    if (!mqtt_client_manager_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_MAX_WAIT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    g_mqtt_context->config.broker.auto_reconnect = enable;
    g_mqtt_context->config.broker.reconnect_timeout_ms = retry_interval_ms;
    g_mqtt_context->config.broker.max_reconnect_attempts = max_attempts;
    
    xSemaphoreGive(g_mqtt_context->mutex);
    
    ESP_LOGI(TAG, "Auto-reconnect %s (interval: %d ms, max attempts: %d)", 
             enable ? "enabled" : "disabled", retry_interval_ms, max_attempts);
    return ESP_OK;
}

esp_err_t mqtt_client_manager_set_ssl_config(const mqtt_client_manager_ssl_config_t* ssl_config)
{
    if (!mqtt_client_manager_is_initialized() || ssl_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (g_mqtt_context->status.connected) {
        ESP_LOGE(TAG, "Cannot change SSL configuration while connected");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_MAX_WAIT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    memcpy(&g_mqtt_context->config.ssl, ssl_config, sizeof(mqtt_client_manager_ssl_config_t));
    
    xSemaphoreGive(g_mqtt_context->mutex);
    
    ESP_LOGI(TAG, "SSL configuration updated");
    return ESP_OK;
}

esp_err_t mqtt_client_manager_set_will(const mqtt_client_manager_will_config_t* will_config)
{
    if (!mqtt_client_manager_is_initialized() || will_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (g_mqtt_context->status.connected) {
        ESP_LOGE(TAG, "Cannot change will configuration while connected");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (will_config->enabled && !mqtt_client_manager_validate_topic(will_config->topic, false)) {
        ESP_LOGE(TAG, "Invalid will topic format");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_MAX_WAIT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    memcpy(&g_mqtt_context->config.will, will_config, sizeof(mqtt_client_manager_will_config_t));
    
    xSemaphoreGive(g_mqtt_context->mutex);
    
    ESP_LOGI(TAG, "Will configuration updated");
    return ESP_OK;
}

esp_err_t mqtt_client_manager_set_message_queue(bool enable, uint16_t queue_size)
{
    if (!mqtt_client_manager_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (queue_size > MQTT_CLIENT_MANAGER_MAX_MESSAGE_QUEUE) {
        ESP_LOGE(TAG, "Queue size too large: %d (max: %d)", queue_size, MQTT_CLIENT_MANAGER_MAX_MESSAGE_QUEUE);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_MAX_WAIT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    g_mqtt_context->config.enable_message_queue = enable;
    g_mqtt_context->config.message_queue_size = queue_size;
    
    xSemaphoreGive(g_mqtt_context->mutex);
    
    ESP_LOGI(TAG, "Message queue %s (size: %d)", enable ? "enabled" : "disabled", queue_size);
    return ESP_OK;
}

uint16_t mqtt_client_manager_get_queue_count(void)
{
    if (!mqtt_client_manager_is_initialized()) {
        return 0;
    }
    
    return uxQueueMessagesWaiting(g_mqtt_context->message_queue);
}

esp_err_t mqtt_client_manager_clear_queue(void)
{
    if (!mqtt_client_manager_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    mqtt_client_manager_queue_item_t queue_item;
    
    /* Remove all messages from queue and free memory */
    while (xQueueReceive(g_mqtt_context->message_queue, &queue_item, 0) == pdTRUE) {
        mqtt_client_manager_free_message(queue_item.message);
    }
    
    ESP_LOGI(TAG, "Message queue cleared");
    return ESP_OK;
}

/* ============================= UTILITY FUNCTIONS ============================= */

const char* mqtt_client_manager_state_to_string(mqtt_client_manager_state_t state)
{
    switch (state) {
        case MQTT_CLIENT_MANAGER_STATE_UNINITIALIZED: return "Uninitialized";
        case MQTT_CLIENT_MANAGER_STATE_INITIALIZED: return "Initialized";
        case MQTT_CLIENT_MANAGER_STATE_CONNECTING: return "Connecting";
        case MQTT_CLIENT_MANAGER_STATE_CONNECTED: return "Connected";
        case MQTT_CLIENT_MANAGER_STATE_DISCONNECTING: return "Disconnecting";
        case MQTT_CLIENT_MANAGER_STATE_DISCONNECTED: return "Disconnected";
        case MQTT_CLIENT_MANAGER_STATE_RECONNECTING: return "Reconnecting";
        case MQTT_CLIENT_MANAGER_STATE_ERROR: return "Error";
        default: return "Unknown";
    }
}

const char* mqtt_client_manager_qos_to_string(mqtt_client_manager_qos_t qos)
{
    switch (qos) {
        case MQTT_CLIENT_MANAGER_QOS_0: return "QoS 0 (At most once)";
        case MQTT_CLIENT_MANAGER_QOS_1: return "QoS 1 (At least once)";
        case MQTT_CLIENT_MANAGER_QOS_2: return "QoS 2 (Exactly once)";
        default: return "Unknown QoS";
    }
}

const char* mqtt_client_manager_transport_to_string(mqtt_client_manager_transport_t transport)
{
    switch (transport) {
        case MQTT_CLIENT_MANAGER_TRANSPORT_TCP: return "TCP";
        case MQTT_CLIENT_MANAGER_TRANSPORT_SSL: return "SSL/TLS";
        case MQTT_CLIENT_MANAGER_TRANSPORT_WS: return "WebSocket";
        case MQTT_CLIENT_MANAGER_TRANSPORT_WSS: return "WebSocket Secure";
        default: return "Unknown";
    }
}

bool mqtt_client_manager_validate_topic(const char* topic, bool allow_wildcards)
{
    if (topic == NULL || strlen(topic) == 0) {
        return false;
    }
    
    size_t len = strlen(topic);
    if (len > MQTT_CLIENT_MANAGER_MAX_TOPIC_LEN - 1) {
        return false;
    }
    
    /* Check for invalid characters */
    for (size_t i = 0; i < len; i++) {
        char c = topic[i];
        
        /* Check for null character */
        if (c == '\0') {
            return false;
        }
        
        /* Check for wildcards */
        if (c == '+' || c == '#') {
            if (!allow_wildcards) {
                return false;
            }
            
            /* Validate wildcard usage */
            if (c == '+') {
                /* Single-level wildcard must be between separators or at boundaries */
                if (i > 0 && topic[i - 1] != '/') {
                    return false;
                }
                if (i < len - 1 && topic[i + 1] != '/') {
                    return false;
                }
            } else if (c == '#') {
                /* Multi-level wildcard must be at the end and preceded by separator */
                if (i != len - 1) {
                    return false;
                }
                if (i > 0 && topic[i - 1] != '/') {
                    return false;
                }
            }
        }
    }
    
    return true;
}

bool mqtt_client_manager_topic_matches(const char* topic, const char* pattern)
{
    if (topic == NULL || pattern == NULL) {
        return false;
    }
    
    const char* topic_ptr = topic;
    const char* pattern_ptr = pattern;
    
    while (*topic_ptr && *pattern_ptr) {
        if (*pattern_ptr == '#') {
            return true; /* Multi-level wildcard matches everything */
        }
        
        if (*pattern_ptr == '+') {
            /* Single-level wildcard - skip to next separator */
            while (*topic_ptr && *topic_ptr != '/') {
                topic_ptr++;
            }
            
            /* Skip the wildcard in pattern */
            pattern_ptr++;
            
            /* If pattern has more but topic ended, no match */
            if (*pattern_ptr && !*topic_ptr) {
                return false;
            }
        } else {
            /* Direct character comparison */
            if (*topic_ptr != *pattern_ptr) {
                return false;
            }
            topic_ptr++;
            pattern_ptr++;
        }
    }
    
    /* Both must be at end for exact match */
    return (*topic_ptr == '\0' && *pattern_ptr == '\0');
}

bool mqtt_client_manager_validate_broker_url(const char* url)
{
    if (url == NULL || strlen(url) == 0) {
        return false;
    }
    
    /* Check for supported schemes */
    if (strncmp(url, "mqtt://", 7) == 0 ||
        strncmp(url, "mqtts://", 8) == 0 ||
        strncmp(url, "ws://", 5) == 0 ||
        strncmp(url, "wss://", 6) == 0) {
        return true;
    }
    
    /* Also accept simple hostname or IP address */
    if (strchr(url, ':') == NULL || strstr(url, "://") == NULL) {
        return true;
    }
    
    return false;
}

mqtt_client_manager_qos_t mqtt_client_manager_get_suggested_qos(const char* topic)
{
    if (topic == NULL) {
        return MQTT_CLIENT_MANAGER_QOS_0;
    }
    
    /* Suggest QoS based on topic patterns */
    if (strstr(topic, "/status") || strstr(topic, "/heartbeat")) {
        return MQTT_CLIENT_MANAGER_QOS_0; /* Status updates don't need guaranteed delivery */
    }
    
    if (strstr(topic, "/command") || strstr(topic, "/control")) {
        return MQTT_CLIENT_MANAGER_QOS_1; /* Commands should be delivered */
    }
    
    if (strstr(topic, "/config") || strstr(topic, "/settings")) {
        return MQTT_CLIENT_MANAGER_QOS_2; /* Configuration changes need exactly-once delivery */
    }
    
    return MQTT_CLIENT_MANAGER_QOS_1; /* Default to QoS 1 for reliability */
}

/* ============================= MESSAGE HANDLING ============================= */

mqtt_client_manager_message_t* mqtt_client_manager_create_message(const char* topic, const char* payload,
                                                                 size_t payload_len, mqtt_client_manager_qos_t qos, bool retain)
{
    if (topic == NULL || payload == NULL) {
        return NULL;
    }
    
    mqtt_client_manager_message_t* message = malloc(sizeof(mqtt_client_manager_message_t));
    if (message == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for message");
        return NULL;
    }
    
    memset(message, 0, sizeof(mqtt_client_manager_message_t));
    
    /* Copy topic */
    strncpy(message->topic, topic, sizeof(message->topic) - 1);
    message->topic[sizeof(message->topic) - 1] = '\0';
    
    /* Determine payload length */
    if (payload_len == 0) {
        payload_len = strlen(payload);
    }
    
    /* Allocate and copy payload */
    message->payload = malloc(payload_len + 1);
    if (message->payload == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for message payload");
        free(message);
        return NULL;
    }
    
    memcpy(message->payload, payload, payload_len);
    message->payload[payload_len] = '\0';
    message->payload_len = payload_len;
    
    /* Set other fields */
    message->qos = qos;
    message->retain = retain;
    message->timestamp_ms = mqtt_client_manager_get_timestamp_ms();
    message->message_id = mqtt_client_manager_generate_message_id();
    message->delivery_status = MQTT_CLIENT_MANAGER_DELIVERY_PENDING;
    
    return message;
}

void mqtt_client_manager_free_message(mqtt_client_manager_message_t* message)
{
    if (message != NULL) {
        if (message->payload != NULL) {
            free(message->payload);
        }
        free(message);
    }
}

mqtt_client_manager_message_t* mqtt_client_manager_clone_message(const mqtt_client_manager_message_t* message)
{
    if (message == NULL) {
        return NULL;
    }
    
    return mqtt_client_manager_create_message(message->topic, message->payload, 
                                            message->payload_len, message->qos, message->retain);
}