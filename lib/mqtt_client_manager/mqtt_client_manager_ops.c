/**
 * @file mqtt_client_manager_ops.c
 * @brief MQTT Client Manager Publish/Subscribe Operations Implementation
 * 
 * This file contains the implementation of publish/subscribe operations,
 * message handling, and callback management for the MQTT Client Manager.
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
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#define TAG "MQTT_CLIENT_OPS"

/* External reference to the main context */
extern mqtt_client_manager_context_t* g_mqtt_context;

/* ============================= PUBLISH OPERATIONS ============================= */

int mqtt_client_manager_publish(const char* topic, const char* payload, size_t payload_len, 
                               mqtt_client_manager_qos_t qos, bool retain)
{
    if (!mqtt_client_manager_is_initialized() || topic == NULL || payload == NULL) {
        ESP_LOGE(TAG, "Invalid arguments for publish");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!mqtt_client_manager_validate_topic(topic, false)) {
        ESP_LOGE(TAG, "Invalid topic format: %s", topic);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!g_mqtt_context->status.connected) {
        ESP_LOGW(TAG, "Not connected to broker, cannot publish");
        
        /* Add to queue if enabled */
        if (g_mqtt_context->config.enable_message_queue) {
            mqtt_client_manager_message_t* message = mqtt_client_manager_create_message(
                topic, payload, payload_len, qos, retain);
            if (message != NULL) {
                esp_err_t ret = mqtt_client_manager_add_to_queue(message);
                if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "Message queued for later delivery");
                    return message->message_id;
                }
                mqtt_client_manager_free_message(message);
            }
        }
        
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Determine payload length if not specified */
    if (payload_len == 0) {
        payload_len = strlen(payload);
    }
    
    if (payload_len > MQTT_CLIENT_MANAGER_MAX_PAYLOAD_LEN) {
        ESP_LOGE(TAG, "Payload too large: %zu bytes (max: %d)", 
                 payload_len, MQTT_CLIENT_MANAGER_MAX_PAYLOAD_LEN);
        return ESP_ERR_INVALID_SIZE;
    }
    
    ESP_LOGD(TAG, "Publishing to topic '%s': %.*s (QoS: %d, Retain: %d)", 
             topic, (int)payload_len, payload, qos, retain);
    
    /* Publish message */
    int msg_id = esp_mqtt_client_publish(g_mqtt_context->mqtt_client, topic, payload, 
                                        payload_len, qos, retain);
    
    if (msg_id >= 0) {
        /* Update statistics */
        g_mqtt_context->status.stats.bytes_sent += payload_len;
        g_mqtt_context->last_activity_time = mqtt_client_manager_get_timestamp_ms();
        
        ESP_LOGD(TAG, "Message published successfully, msg_id: %d", msg_id);
    } else {
        ESP_LOGE(TAG, "Failed to publish message, error: %d", msg_id);
        g_mqtt_context->status.stats.messages_failed++;
    }
    
    return msg_id;
}

int mqtt_client_manager_publish_simple(const char* topic, const char* payload)
{
    return mqtt_client_manager_publish(topic, payload, 0, 
                                     g_mqtt_context->config.default_qos, 
                                     g_mqtt_context->config.default_retain);
}

int mqtt_client_manager_publish_json(const char* topic, const char* json_payload, 
                                    mqtt_client_manager_qos_t qos, bool retain)
{
    if (json_payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Validate JSON */
    cJSON* json = cJSON_Parse(json_payload);
    if (json == NULL) {
        ESP_LOGE(TAG, "Invalid JSON payload");
        return ESP_ERR_INVALID_ARG;
    }
    cJSON_Delete(json);
    
    return mqtt_client_manager_publish(topic, json_payload, 0, qos, retain);
}

int mqtt_client_manager_publish_formatted(const char* topic, mqtt_client_manager_qos_t qos, 
                                         bool retain, const char* format, ...)
{
    if (topic == NULL || format == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Create formatted message */
    va_list args;
    va_start(args, format);
    
    char* buffer = malloc(MQTT_CLIENT_MANAGER_MAX_PAYLOAD_LEN);
    if (buffer == NULL) {
        va_end(args);
        return ESP_ERR_NO_MEM;
    }
    
    int len = vsnprintf(buffer, MQTT_CLIENT_MANAGER_MAX_PAYLOAD_LEN, format, args);
    va_end(args);
    
    if (len < 0 || len >= MQTT_CLIENT_MANAGER_MAX_PAYLOAD_LEN) {
        ESP_LOGE(TAG, "Formatted message too long or formatting error");
        free(buffer);
        return ESP_ERR_INVALID_SIZE;
    }
    
    int result = mqtt_client_manager_publish(topic, buffer, len, qos, retain);
    free(buffer);
    
    return result;
}

/* ============================= SUBSCRIBE OPERATIONS ============================= */

int mqtt_client_manager_subscribe(const char* topic, mqtt_client_manager_qos_t qos)
{
    if (!mqtt_client_manager_is_initialized() || topic == NULL) {
        ESP_LOGE(TAG, "Invalid arguments for subscribe");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!mqtt_client_manager_validate_topic(topic, true)) {
        ESP_LOGE(TAG, "Invalid topic format: %s", topic);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!g_mqtt_context->status.connected) {
        ESP_LOGE(TAG, "Not connected to broker, cannot subscribe");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Subscribing to topic: %s (QoS: %d)", topic, qos);
    
    /* Check if already subscribed */
    mqtt_client_manager_subscription_t* existing = mqtt_client_manager_find_subscription(topic);
    if (existing != NULL) {
        ESP_LOGW(TAG, "Already subscribed to topic: %s", topic);
        return ESP_OK;
    }
    
    /* Subscribe to topic */
    int msg_id = esp_mqtt_client_subscribe(g_mqtt_context->mqtt_client, topic, qos);
    
    if (msg_id >= 0) {
        /* Add subscription to list */
        esp_err_t ret = mqtt_client_manager_add_subscription(topic, qos);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to track subscription: %s", esp_err_to_name(ret));
        }
        
        ESP_LOGI(TAG, "Subscription request sent, msg_id: %d", msg_id);
    } else {
        ESP_LOGE(TAG, "Failed to subscribe to topic: %s, error: %d", topic, msg_id);
    }
    
    return msg_id;
}

int mqtt_client_manager_subscribe_with_callback(const char* topic, mqtt_client_manager_qos_t qos,
                                               mqtt_client_manager_topic_callback_t callback, void* user_data)
{
    if (callback == NULL) {
        ESP_LOGE(TAG, "Callback cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    /* First subscribe to the topic */
    int msg_id = mqtt_client_manager_subscribe(topic, qos);
    if (msg_id < 0) {
        return msg_id;
    }
    
    /* Register topic-specific callback */
    mqtt_client_manager_topic_callback_node_t* node = malloc(sizeof(mqtt_client_manager_topic_callback_node_t));
    if (node == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for topic callback");
        return ESP_ERR_NO_MEM;
    }
    
    strncpy(node->topic, topic, sizeof(node->topic) - 1);
    node->topic[sizeof(node->topic) - 1] = '\0';
    node->callback = callback;
    node->user_data = user_data;
    
    /* Add to linked list */
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_MAX_WAIT_MS)) == pdTRUE) {
        node->next = g_mqtt_context->topic_callbacks;
        g_mqtt_context->topic_callbacks = node;
        xSemaphoreGive(g_mqtt_context->mutex);
        
        ESP_LOGD(TAG, "Topic callback registered for: %s", topic);
    } else {
        free(node);
        ESP_LOGE(TAG, "Failed to acquire mutex for callback registration");
        return ESP_ERR_TIMEOUT;
    }
    
    return msg_id;
}

int mqtt_client_manager_unsubscribe(const char* topic)
{
    if (!mqtt_client_manager_is_initialized() || topic == NULL) {
        ESP_LOGE(TAG, "Invalid arguments for unsubscribe");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!g_mqtt_context->status.connected) {
        ESP_LOGE(TAG, "Not connected to broker, cannot unsubscribe");
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Check if subscribed */
    mqtt_client_manager_subscription_t* subscription = mqtt_client_manager_find_subscription(topic);
    if (subscription == NULL) {
        ESP_LOGW(TAG, "Not subscribed to topic: %s", topic);
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "Unsubscribing from topic: %s", topic);
    
    /* Unsubscribe from topic */
    int msg_id = esp_mqtt_client_unsubscribe(g_mqtt_context->mqtt_client, topic);
    
    if (msg_id >= 0) {
        /* Remove subscription from list */
        esp_err_t ret = mqtt_client_manager_remove_subscription(topic);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to remove subscription tracking: %s", esp_err_to_name(ret));
        }
        
        /* Remove topic-specific callback if exists */
        if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_MAX_WAIT_MS)) == pdTRUE) {
            mqtt_client_manager_topic_callback_node_t** current = &g_mqtt_context->topic_callbacks;
            while (*current != NULL) {
                if (strcmp((*current)->topic, topic) == 0) {
                    mqtt_client_manager_topic_callback_node_t* to_remove = *current;
                    *current = (*current)->next;
                    free(to_remove);
                    ESP_LOGD(TAG, "Topic callback removed for: %s", topic);
                    break;
                }
                current = &(*current)->next;
            }
            xSemaphoreGive(g_mqtt_context->mutex);
        }
        
        ESP_LOGI(TAG, "Unsubscription request sent, msg_id: %d", msg_id);
    } else {
        ESP_LOGE(TAG, "Failed to unsubscribe from topic: %s, error: %d", topic, msg_id);
    }
    
    return msg_id;
}

esp_err_t mqtt_client_manager_unsubscribe_all(void)
{
    if (!mqtt_client_manager_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!g_mqtt_context->status.connected) {
        ESP_LOGE(TAG, "Not connected to broker, cannot unsubscribe");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Unsubscribing from all topics");
    
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_MAX_WAIT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    /* Unsubscribe from all active subscriptions */
    for (size_t i = 0; i < g_mqtt_context->subscription_count; i++) {
        if (g_mqtt_context->subscriptions[i].active) {
            esp_mqtt_client_unsubscribe(g_mqtt_context->mqtt_client, 
                                       g_mqtt_context->subscriptions[i].topic);
            g_mqtt_context->subscriptions[i].active = false;
        }
    }
    
    g_mqtt_context->subscription_count = 0;
    g_mqtt_context->status.stats.subscription_count = 0;
    
    /* Clear all topic callbacks */
    mqtt_client_manager_topic_callback_node_t* node = g_mqtt_context->topic_callbacks;
    while (node != NULL) {
        mqtt_client_manager_topic_callback_node_t* next = node->next;
        free(node);
        node = next;
    }
    g_mqtt_context->topic_callbacks = NULL;
    
    xSemaphoreGive(g_mqtt_context->mutex);
    
    ESP_LOGI(TAG, "All subscriptions cleared");
    return ESP_OK;
}

esp_err_t mqtt_client_manager_get_subscriptions(mqtt_client_manager_subscription_t* subscriptions,
                                               size_t max_subscriptions, size_t* actual_count)
{
    if (!mqtt_client_manager_is_initialized() || subscriptions == NULL || actual_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_MAX_WAIT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    size_t count = 0;
    for (size_t i = 0; i < g_mqtt_context->subscription_count && count < max_subscriptions; i++) {
        if (g_mqtt_context->subscriptions[i].active) {
            memcpy(&subscriptions[count], &g_mqtt_context->subscriptions[i], 
                   sizeof(mqtt_client_manager_subscription_t));
            count++;
        }
    }
    
    *actual_count = count;
    
    xSemaphoreGive(g_mqtt_context->mutex);
    return ESP_OK;
}

/* ============================= MESSAGE HANDLING ============================= */

static esp_err_t mqtt_client_manager_process_message_queue(void)
{
    if (!g_mqtt_context->config.enable_message_queue) {
        return ESP_OK;
    }
    
    mqtt_client_manager_queue_item_t queue_item;
    
    /* Process messages from queue */
    while (xQueueReceive(g_mqtt_context->message_queue, &queue_item, 0) == pdTRUE) {
        if (!g_mqtt_context->status.connected) {
            /* Put message back in queue if not connected */
            if (xQueueSend(g_mqtt_context->message_queue, &queue_item, 0) != pdTRUE) {
                ESP_LOGW(TAG, "Failed to re-queue message, discarding");
                mqtt_client_manager_free_message(queue_item.message);
            }
            break;
        }
        
        /* Try to publish the queued message */
        int result = esp_mqtt_client_publish(g_mqtt_context->mqtt_client,
                                           queue_item.message->topic,
                                           queue_item.message->payload,
                                           queue_item.message->payload_len,
                                           queue_item.message->qos,
                                           queue_item.message->retain);
        
        if (result >= 0) {
            ESP_LOGD(TAG, "Queued message published successfully");
            mqtt_client_manager_free_message(queue_item.message);
        } else {
            queue_item.retry_count++;
            
            if (queue_item.retry_count < 3) {
                /* Re-queue for retry */
                if (xQueueSend(g_mqtt_context->message_queue, &queue_item, 0) != pdTRUE) {
                    ESP_LOGW(TAG, "Failed to re-queue message for retry, discarding");
                    mqtt_client_manager_free_message(queue_item.message);
                }
            } else {
                ESP_LOGW(TAG, "Message publish failed after retries, discarding");
                mqtt_client_manager_free_message(queue_item.message);
            }
        }
    }
    
    return ESP_OK;
}

static esp_err_t mqtt_client_manager_add_to_queue(mqtt_client_manager_message_t* message)
{
    if (message == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    mqtt_client_manager_queue_item_t queue_item = {
        .message = message,
        .timestamp_ms = mqtt_client_manager_get_timestamp_ms(),
        .retry_count = 0,
        .persistent = false
    };
    
    if (xQueueSend(g_mqtt_context->message_queue, &queue_item, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Message queue full, discarding oldest message");
        
        /* Try to remove oldest message and add new one */
        mqtt_client_manager_queue_item_t old_item;
        if (xQueueReceive(g_mqtt_context->message_queue, &old_item, 0) == pdTRUE) {
            mqtt_client_manager_free_message(old_item.message);
            
            if (xQueueSend(g_mqtt_context->message_queue, &queue_item, 0) != pdTRUE) {
                ESP_LOGE(TAG, "Failed to add message to queue after clearing space");
                return ESP_FAIL;
            }
        } else {
            ESP_LOGE(TAG, "Failed to clear space in message queue");
            return ESP_FAIL;
        }
    }
    
    return ESP_OK;
}

static mqtt_client_manager_message_t* mqtt_client_manager_remove_from_queue(void)
{
    mqtt_client_manager_queue_item_t queue_item;
    
    if (xQueueReceive(g_mqtt_context->message_queue, &queue_item, 0) == pdTRUE) {
        return queue_item.message;
    }
    
    return NULL;
}

/* ============================= SUBSCRIPTION MANAGEMENT ============================= */

static esp_err_t mqtt_client_manager_add_subscription(const char* topic, mqtt_client_manager_qos_t qos)
{
    if (topic == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_MAX_WAIT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    /* Check if subscription list is full */
    if (g_mqtt_context->subscription_count >= MQTT_CLIENT_MANAGER_MAX_SUBSCRIPTIONS) {
        xSemaphoreGive(g_mqtt_context->mutex);
        ESP_LOGE(TAG, "Maximum number of subscriptions reached");
        return ESP_ERR_NO_MEM;
    }
    
    /* Add new subscription */
    mqtt_client_manager_subscription_t* sub = &g_mqtt_context->subscriptions[g_mqtt_context->subscription_count];
    
    strncpy(sub->topic, topic, sizeof(sub->topic) - 1);
    sub->topic[sizeof(sub->topic) - 1] = '\0';
    sub->qos = qos;
    sub->user_data = NULL;
    sub->active = true;
    sub->message_count = 0;
    sub->last_message_time = 0;
    
    g_mqtt_context->subscription_count++;
    g_mqtt_context->status.stats.subscription_count = g_mqtt_context->subscription_count;
    
    xSemaphoreGive(g_mqtt_context->mutex);
    
    ESP_LOGD(TAG, "Subscription added: %s (QoS: %d)", topic, qos);
    return ESP_OK;
}

static esp_err_t mqtt_client_manager_remove_subscription(const char* topic)
{
    if (topic == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(MQTT_CLIENT_MANAGER_MAX_WAIT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    /* Find and remove subscription */
    for (size_t i = 0; i < g_mqtt_context->subscription_count; i++) {
        if (strcmp(g_mqtt_context->subscriptions[i].topic, topic) == 0) {
            /* Move remaining subscriptions */
            for (size_t j = i; j < g_mqtt_context->subscription_count - 1; j++) {
                memcpy(&g_mqtt_context->subscriptions[j], &g_mqtt_context->subscriptions[j + 1],
                       sizeof(mqtt_client_manager_subscription_t));
            }
            
            g_mqtt_context->subscription_count--;
            g_mqtt_context->status.stats.subscription_count = g_mqtt_context->subscription_count;
            
            xSemaphoreGive(g_mqtt_context->mutex);
            
            ESP_LOGD(TAG, "Subscription removed: %s", topic);
            return ESP_OK;
        }
    }
    
    xSemaphoreGive(g_mqtt_context->mutex);
    return ESP_ERR_NOT_FOUND;
}

static mqtt_client_manager_subscription_t* mqtt_client_manager_find_subscription(const char* topic)
{
    if (topic == NULL) {
        return NULL;
    }
    
    for (size_t i = 0; i < g_mqtt_context->subscription_count; i++) {
        if (g_mqtt_context->subscriptions[i].active &&
            strcmp(g_mqtt_context->subscriptions[i].topic, topic) == 0) {
            return &g_mqtt_context->subscriptions[i];
        }
    }
    
    return NULL;
}

/* ============================= EVENT/CALLBACK MANAGEMENT ============================= */

static esp_err_t mqtt_client_manager_fire_event(mqtt_client_manager_event_t event, void* data)
{
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    mqtt_client_manager_event_callback_node_t* node = g_mqtt_context->event_callbacks;
    while (node != NULL) {
        if (node->callback != NULL) {
            node->callback(event, data, node->user_data);
        }
        node = node->next;
    }
    
    xSemaphoreGive(g_mqtt_context->mutex);
    return ESP_OK;
}

static esp_err_t mqtt_client_manager_fire_message_callbacks(const mqtt_client_manager_message_t* message)
{
    if (message == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    mqtt_client_manager_message_callback_node_t* node = g_mqtt_context->message_callbacks;
    while (node != NULL) {
        if (node->callback != NULL) {
            node->callback(message, node->user_data);
        }
        node = node->next;
    }
    
    xSemaphoreGive(g_mqtt_context->mutex);
    return ESP_OK;
}

static esp_err_t mqtt_client_manager_fire_topic_callbacks(const char* topic, const char* payload, size_t payload_len)
{
    if (topic == NULL || payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_mqtt_context->mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    mqtt_client_manager_topic_callback_node_t* node = g_mqtt_context->topic_callbacks;
    while (node != NULL) {
        if (node->callback != NULL && mqtt_client_manager_topic_matches(topic, node->topic)) {
            node->callback(topic, payload, payload_len, node->user_data);
        }
        node = node->next;
    }
    
    xSemaphoreGive(g_mqtt_context->mutex);
    return ESP_OK;
}

/* ============================= RECONNECTION LOGIC ============================= */

static esp_err_t mqtt_client_manager_check_reconnection(void)
{
    if (!g_mqtt_context->config.broker.auto_reconnect) {
        return ESP_OK;
    }
    
    if (g_mqtt_context->status.state != MQTT_CLIENT_MANAGER_STATE_RECONNECTING) {
        return ESP_OK;
    }
    
    uint32_t current_time = mqtt_client_manager_get_timestamp_ms();
    
    if (current_time >= g_mqtt_context->next_reconnect_time) {
        /* Check if we've exceeded maximum attempts */
        if (g_mqtt_context->config.broker.max_reconnect_attempts > 0 &&
            g_mqtt_context->reconnect_attempts >= g_mqtt_context->config.broker.max_reconnect_attempts) {
            ESP_LOGE(TAG, "Maximum reconnection attempts reached, giving up");
            g_mqtt_context->status.state = MQTT_CLIENT_MANAGER_STATE_ERROR;
            strcpy(g_mqtt_context->status.status_message, "Max reconnect attempts reached");
            return ESP_FAIL;
        }
        
        ESP_LOGI(TAG, "Attempting reconnection (attempt %d)", g_mqtt_context->reconnect_attempts + 1);
        
        g_mqtt_context->reconnect_attempts++;
        g_mqtt_context->status.stats.reconnections++;
        
        /* Try to reconnect */
        esp_err_t ret = mqtt_client_manager_connect();
        if (ret != ESP_OK) {
            /* Calculate next reconnection time with exponential backoff */
            uint32_t backoff = g_mqtt_context->config.broker.reconnect_timeout_ms * 
                              (1 << (g_mqtt_context->reconnect_attempts - 1));
            
            if (backoff > g_mqtt_context->config.broker.max_reconnect_interval_ms) {
                backoff = g_mqtt_context->config.broker.max_reconnect_interval_ms;
            }
            
            g_mqtt_context->next_reconnect_time = current_time + backoff;
            
            ESP_LOGW(TAG, "Reconnection failed, next attempt in %d ms", backoff);
        }
    }
    
    return ESP_OK;
}