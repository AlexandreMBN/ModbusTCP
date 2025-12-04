/**
 * @file mqtt_client_manager.h
 * @brief Comprehensive MQTT Client Manager Library for ESP32
 * 
 * This library provides a complete MQTT client solution for ESP32 projects with:
 * - Advanced connection management with auto-reconnection
 * - Publish/Subscribe operations with QoS support
 * - SSL/TLS encryption with certificate management
 * - Topic routing and pattern matching
 * - Configuration management integration
 * - Message queuing and buffering
 * - Event-driven architecture
 * 
 * Features:
 * - Multi-broker support
 * - Automatic reconnection with backoff
 * - Message persistence and delivery guarantees
 * - Topic subscription management
 * - Will messages and last will testament
 * - Retained message handling
 * - Connection state monitoring
 * - Thread-safe operations
 * - Configuration persistence
 * - SSL/TLS with CA certificate validation
 * 
 * @version 1.0.0
 * @date 2024-11-10
 * @author ESP32 Development Team
 * 
 * @copyright Copyright (c) 2024 ESP32 Development Team
 * Licensed under the MIT License.
 */

#ifndef MQTT_CLIENT_MANAGER_H
#define MQTT_CLIENT_MANAGER_H

#include <esp_err.h>
#include <mqtt_client.h>
#include <stdbool.h>
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================= CONSTANTS ============================= */

#define MQTT_CLIENT_MANAGER_VERSION "1.0.0"

/* Configuration limits */
#define MQTT_CLIENT_MANAGER_MAX_BROKER_URL_LEN     256   /**< Maximum broker URL length */
#define MQTT_CLIENT_MANAGER_MAX_CLIENT_ID_LEN      64    /**< Maximum client ID length */
#define MQTT_CLIENT_MANAGER_MAX_USERNAME_LEN       64    /**< Maximum username length */
#define MQTT_CLIENT_MANAGER_MAX_PASSWORD_LEN       64    /**< Maximum password length */
#define MQTT_CLIENT_MANAGER_MAX_TOPIC_LEN          128   /**< Maximum topic length */
#define MQTT_CLIENT_MANAGER_MAX_PAYLOAD_LEN        1024  /**< Maximum payload length */
#define MQTT_CLIENT_MANAGER_MAX_CA_PATH_LEN        256   /**< Maximum CA certificate path length */
#define MQTT_CLIENT_MANAGER_MAX_SUBSCRIPTIONS      32    /**< Maximum concurrent subscriptions */
#define MQTT_CLIENT_MANAGER_MAX_MESSAGE_QUEUE      50    /**< Maximum queued messages */

/* Default values */
#define MQTT_CLIENT_MANAGER_DEFAULT_PORT           1883  /**< Default MQTT port */
#define MQTT_CLIENT_MANAGER_DEFAULT_TLS_PORT       8883  /**< Default MQTTS port */
#define MQTT_CLIENT_MANAGER_DEFAULT_WS_PORT        8000  /**< Default WebSocket port */
#define MQTT_CLIENT_MANAGER_DEFAULT_WSS_PORT       443   /**< Default WebSocket Secure port */
#define MQTT_CLIENT_MANAGER_DEFAULT_KEEPALIVE      60    /**< Default keepalive interval */
#define MQTT_CLIENT_MANAGER_DEFAULT_QOS            1     /**< Default QoS level */
#define MQTT_CLIENT_MANAGER_DEFAULT_RETRY_INTERVAL 5000  /**< Default retry interval (ms) */
#define MQTT_CLIENT_MANAGER_DEFAULT_CONNECT_TIMEOUT 10000 /**< Default connection timeout (ms) */

/* Topic constants */
#define MQTT_CLIENT_MANAGER_TOPIC_SEPARATOR        "/"
#define MQTT_CLIENT_MANAGER_WILDCARD_SINGLE        "+"
#define MQTT_CLIENT_MANAGER_WILDCARD_MULTI         "#"

/* ============================= ENUMERATIONS ============================= */

/**
 * @brief MQTT Client Manager states
 */
typedef enum {
    MQTT_CLIENT_MANAGER_STATE_UNINITIALIZED = 0,  /**< Not initialized */
    MQTT_CLIENT_MANAGER_STATE_INITIALIZED,        /**< Initialized but not connected */
    MQTT_CLIENT_MANAGER_STATE_CONNECTING,         /**< Connection attempt in progress */
    MQTT_CLIENT_MANAGER_STATE_CONNECTED,          /**< Connected to broker */
    MQTT_CLIENT_MANAGER_STATE_DISCONNECTING,      /**< Disconnection in progress */
    MQTT_CLIENT_MANAGER_STATE_DISCONNECTED,       /**< Disconnected from broker */
    MQTT_CLIENT_MANAGER_STATE_RECONNECTING,       /**< Reconnection attempt in progress */
    MQTT_CLIENT_MANAGER_STATE_ERROR               /**< Error state */
} mqtt_client_manager_state_t;

/**
 * @brief MQTT QoS levels
 */
typedef enum {
    MQTT_CLIENT_MANAGER_QOS_0 = 0,                /**< At most once delivery */
    MQTT_CLIENT_MANAGER_QOS_1 = 1,                /**< At least once delivery */
    MQTT_CLIENT_MANAGER_QOS_2 = 2                 /**< Exactly once delivery */
} mqtt_client_manager_qos_t;

/**
 * @brief MQTT connection security types
 */
typedef enum {
    MQTT_CLIENT_MANAGER_TRANSPORT_TCP = 0,        /**< Plain TCP connection */
    MQTT_CLIENT_MANAGER_TRANSPORT_SSL,            /**< SSL/TLS encrypted connection */
    MQTT_CLIENT_MANAGER_TRANSPORT_WS,             /**< WebSocket connection */
    MQTT_CLIENT_MANAGER_TRANSPORT_WSS             /**< WebSocket Secure connection */
} mqtt_client_manager_transport_t;

/**
 * @brief MQTT event types
 */
typedef enum {
    MQTT_CLIENT_MANAGER_EVENT_BEFORE_CONNECT = 0, /**< Before connection attempt */
    MQTT_CLIENT_MANAGER_EVENT_CONNECTED,          /**< Connected to broker */
    MQTT_CLIENT_MANAGER_EVENT_DISCONNECTED,       /**< Disconnected from broker */
    MQTT_CLIENT_MANAGER_EVENT_SUBSCRIBED,         /**< Successfully subscribed to topic */
    MQTT_CLIENT_MANAGER_EVENT_UNSUBSCRIBED,       /**< Successfully unsubscribed from topic */
    MQTT_CLIENT_MANAGER_EVENT_PUBLISHED,          /**< Message published successfully */
    MQTT_CLIENT_MANAGER_EVENT_MESSAGE_RECEIVED,   /**< Message received */
    MQTT_CLIENT_MANAGER_EVENT_ERROR,              /**< Error occurred */
    MQTT_CLIENT_MANAGER_EVENT_CONNECTION_LOST,    /**< Connection lost (will auto-reconnect) */
    MQTT_CLIENT_MANAGER_EVENT_RECONNECTED         /**< Reconnected after connection loss */
} mqtt_client_manager_event_t;

/**
 * @brief Message delivery status
 */
typedef enum {
    MQTT_CLIENT_MANAGER_DELIVERY_UNKNOWN = 0,     /**< Delivery status unknown */
    MQTT_CLIENT_MANAGER_DELIVERY_PENDING,         /**< Message queued for delivery */
    MQTT_CLIENT_MANAGER_DELIVERY_IN_TRANSIT,      /**< Message being transmitted */
    MQTT_CLIENT_MANAGER_DELIVERY_DELIVERED,       /**< Message delivered successfully */
    MQTT_CLIENT_MANAGER_DELIVERY_FAILED           /**< Message delivery failed */
} mqtt_client_manager_delivery_status_t;

/* ============================= STRUCTURES ============================= */

/**
 * @brief MQTT broker configuration
 */
typedef struct {
    char broker_url[MQTT_CLIENT_MANAGER_MAX_BROKER_URL_LEN];  /**< Broker URL */
    uint16_t port;                                           /**< Broker port */
    mqtt_client_manager_transport_t transport;              /**< Transport type */
    char client_id[MQTT_CLIENT_MANAGER_MAX_CLIENT_ID_LEN];   /**< Client identifier */
    char username[MQTT_CLIENT_MANAGER_MAX_USERNAME_LEN];     /**< Username for authentication */
    char password[MQTT_CLIENT_MANAGER_MAX_PASSWORD_LEN];     /**< Password for authentication */
    uint16_t keepalive;                                      /**< Keepalive interval in seconds */
    bool clean_session;                                      /**< Clean session flag */
    uint32_t connect_timeout_ms;                             /**< Connection timeout */
    uint32_t network_timeout_ms;                             /**< Network operation timeout */
    uint16_t buffer_size;                                    /**< Internal buffer size */
    bool auto_reconnect;                                     /**< Enable automatic reconnection */
    uint32_t reconnect_timeout_ms;                           /**< Reconnection interval */
    uint32_t max_reconnect_interval_ms;                      /**< Maximum reconnection interval */
    uint8_t max_reconnect_attempts;                          /**< Maximum reconnection attempts (0 = infinite) */
} mqtt_client_manager_broker_config_t;

/**
 * @brief SSL/TLS configuration
 */
typedef struct {
    bool enabled;                                            /**< Enable SSL/TLS */
    char ca_cert_path[MQTT_CLIENT_MANAGER_MAX_CA_PATH_LEN];  /**< CA certificate file path */
    char* ca_cert_pem;                                       /**< CA certificate PEM data */
    size_t ca_cert_len;                                      /**< CA certificate length */
    char client_cert_path[MQTT_CLIENT_MANAGER_MAX_CA_PATH_LEN]; /**< Client certificate path */
    char* client_cert_pem;                                   /**< Client certificate PEM data */
    size_t client_cert_len;                                  /**< Client certificate length */
    char client_key_path[MQTT_CLIENT_MANAGER_MAX_CA_PATH_LEN];  /**< Client private key path */
    char* client_key_pem;                                    /**< Client private key PEM data */
    size_t client_key_len;                                   /**< Client private key length */
    bool verify_peer;                                        /**< Verify broker certificate */
    bool verify_hostname;                                    /**< Verify broker hostname */
    bool use_global_ca_store;                                /**< Use ESP-IDF global CA store */
    bool skip_cert_common_name_check;                        /**< Skip common name verification */
    char* alpn_protos[4];                                    /**< ALPN protocol list */
} mqtt_client_manager_ssl_config_t;

/**
 * @brief Last Will and Testament configuration
 */
typedef struct {
    bool enabled;                                            /**< Enable last will */
    char topic[MQTT_CLIENT_MANAGER_MAX_TOPIC_LEN];           /**< Will topic */
    char* message;                                           /**< Will message payload */
    size_t message_len;                                      /**< Will message length */
    mqtt_client_manager_qos_t qos;                           /**< Will message QoS */
    bool retain;                                             /**< Will message retain flag */
} mqtt_client_manager_will_config_t;

/**
 * @brief MQTT message structure
 */
typedef struct {
    char topic[MQTT_CLIENT_MANAGER_MAX_TOPIC_LEN];           /**< Message topic */
    char* payload;                                           /**< Message payload */
    size_t payload_len;                                      /**< Payload length */
    mqtt_client_manager_qos_t qos;                           /**< Quality of Service */
    bool retain;                                             /**< Retain message flag */
    uint32_t timestamp_ms;                                   /**< Message timestamp */
    int message_id;                                          /**< Message identifier */
    mqtt_client_manager_delivery_status_t delivery_status;  /**< Delivery status */
} mqtt_client_manager_message_t;

/**
 * @brief Topic subscription information
 */
typedef struct {
    char topic[MQTT_CLIENT_MANAGER_MAX_TOPIC_LEN];           /**< Subscribed topic */
    mqtt_client_manager_qos_t qos;                           /**< Subscription QoS */
    void* user_data;                                         /**< User data for callback */
    bool active;                                             /**< Subscription active status */
    uint32_t message_count;                                  /**< Number of messages received */
    uint32_t last_message_time;                              /**< Last message timestamp */
} mqtt_client_manager_subscription_t;

/**
 * @brief MQTT Client Manager statistics
 */
typedef struct {
    uint32_t messages_published;                             /**< Total messages published */
    uint32_t messages_received;                              /**< Total messages received */
    uint32_t messages_failed;                                /**< Failed message deliveries */
    uint32_t connection_attempts;                            /**< Total connection attempts */
    uint32_t successful_connections;                         /**< Successful connections */
    uint32_t disconnections;                                 /**< Total disconnections */
    uint32_t reconnections;                                  /**< Automatic reconnections */
    uint32_t subscription_count;                             /**< Active subscriptions */
    uint32_t uptime_ms;                                      /**< Connection uptime */
    uint32_t total_uptime_ms;                                /**< Total uptime since init */
    size_t bytes_sent;                                       /**< Total bytes sent */
    size_t bytes_received;                                   /**< Total bytes received */
} mqtt_client_manager_stats_t;

/**
 * @brief MQTT Client Manager status
 */
typedef struct {
    mqtt_client_manager_state_t state;                       /**< Current connection state */
    bool connected;                                          /**< Connection status */
    char broker_url[MQTT_CLIENT_MANAGER_MAX_BROKER_URL_LEN]; /**< Connected broker URL */
    uint16_t broker_port;                                    /**< Connected broker port */
    char client_id[MQTT_CLIENT_MANAGER_MAX_CLIENT_ID_LEN];   /**< Client ID in use */
    mqtt_client_manager_transport_t transport;              /**< Active transport type */
    uint32_t connection_time_ms;                             /**< Connection establishment time */
    uint32_t last_activity_ms;                               /**< Last activity timestamp */
    uint32_t next_reconnect_ms;                              /**< Next reconnection attempt time */
    uint8_t reconnect_attempts;                              /**< Current reconnection attempts */
    mqtt_client_manager_stats_t stats;                       /**< Connection statistics */
    char status_message[128];                                /**< Human-readable status */
} mqtt_client_manager_status_t;

/**
 * @brief Main MQTT Client Manager configuration
 */
typedef struct {
    mqtt_client_manager_broker_config_t broker;              /**< Broker configuration */
    mqtt_client_manager_ssl_config_t ssl;                    /**< SSL/TLS configuration */
    mqtt_client_manager_will_config_t will;                  /**< Last will configuration */
    bool enabled;                                            /**< Enable MQTT client */
    mqtt_client_manager_qos_t default_qos;                   /**< Default QoS level */
    bool default_retain;                                     /**< Default retain flag */
    uint32_t publish_timeout_ms;                             /**< Publish operation timeout */
    uint32_t subscribe_timeout_ms;                           /**< Subscribe operation timeout */
    uint16_t max_inflight_messages;                          /**< Maximum in-flight messages */
    bool enable_message_queue;                               /**< Enable message queuing */
    uint16_t message_queue_size;                             /**< Message queue size */
    bool persist_session;                                    /**< Persist session across restarts */
    bool enable_metrics;                                     /**< Enable performance metrics */
} mqtt_client_manager_config_t;

/**
 * @brief Event callback function type
 * 
 * @param event Event type
 * @param data Event-specific data (can be NULL)
 * @param user_data User-provided data
 */
typedef void (*mqtt_client_manager_event_callback_t)(mqtt_client_manager_event_t event, void* data, void* user_data);

/**
 * @brief Message callback function type
 * 
 * @param message Received message
 * @param user_data User-provided data
 */
typedef void (*mqtt_client_manager_message_callback_t)(const mqtt_client_manager_message_t* message, void* user_data);

/**
 * @brief Topic-specific message callback function type
 * 
 * @param topic Message topic
 * @param payload Message payload
 * @param payload_len Payload length
 * @param user_data User-provided data
 */
typedef void (*mqtt_client_manager_topic_callback_t)(const char* topic, const char* payload, size_t payload_len, void* user_data);

/* ============================= INITIALIZATION ============================= */

/**
 * @brief Initialize MQTT Client Manager with default configuration
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Already initialized
 *     - ESP_ERR_NO_MEM: Memory allocation failed
 *     - Other ESP error codes
 */
esp_err_t mqtt_client_manager_init(void);

/**
 * @brief Initialize MQTT Client Manager with custom configuration
 * 
 * @param config MQTT Client Manager configuration
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid configuration
 *     - ESP_ERR_INVALID_STATE: Already initialized
 *     - ESP_ERR_NO_MEM: Memory allocation failed
 *     - Other ESP error codes
 */
esp_err_t mqtt_client_manager_init_with_config(const mqtt_client_manager_config_t* config);

/**
 * @brief Deinitialize MQTT Client Manager and cleanup resources
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t mqtt_client_manager_deinit(void);

/**
 * @brief Check if MQTT Client Manager is initialized
 * 
 * @return true if initialized, false otherwise
 */
bool mqtt_client_manager_is_initialized(void);

/* ============================= CONFIGURATION ============================= */

/**
 * @brief Get default MQTT Client Manager configuration
 * 
 * @param config Pointer to configuration structure to fill
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid argument
 */
esp_err_t mqtt_client_manager_get_default_config(mqtt_client_manager_config_t* config);

/**
 * @brief Set MQTT Client Manager configuration
 * 
 * @param config New configuration
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid configuration
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t mqtt_client_manager_set_config(const mqtt_client_manager_config_t* config);

/**
 * @brief Get current MQTT Client Manager configuration
 * 
 * @param config Pointer to configuration structure to fill
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid argument
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t mqtt_client_manager_get_config(mqtt_client_manager_config_t* config);

/**
 * @brief Load configuration from storage
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_NOT_FOUND: Configuration not found
 *     - ESP_ERR_INVALID_STATE: Not initialized
 *     - Other ESP error codes
 */
esp_err_t mqtt_client_manager_load_config(void);

/**
 * @brief Save configuration to storage
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not initialized
 *     - Other ESP error codes
 */
esp_err_t mqtt_client_manager_save_config(void);

/* ============================= CONNECTION MANAGEMENT ============================= */

/**
 * @brief Connect to MQTT broker
 * 
 * @return 
 *     - ESP_OK: Success (connection initiated)
 *     - ESP_ERR_INVALID_STATE: Invalid state for operation
 *     - ESP_ERR_INVALID_ARG: Invalid broker configuration
 *     - Other ESP error codes
 */
esp_err_t mqtt_client_manager_connect(void);

/**
 * @brief Connect to MQTT broker with custom configuration
 * 
 * @param broker_config Broker configuration
 * @return 
 *     - ESP_OK: Success (connection initiated)
 *     - ESP_ERR_INVALID_ARG: Invalid broker configuration
 *     - ESP_ERR_INVALID_STATE: Invalid state for operation
 *     - Other ESP error codes
 */
esp_err_t mqtt_client_manager_connect_with_config(const mqtt_client_manager_broker_config_t* broker_config);

/**
 * @brief Disconnect from MQTT broker
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not connected
 */
esp_err_t mqtt_client_manager_disconnect(void);

/**
 * @brief Reconnect to MQTT broker
 * 
 * @return 
 *     - ESP_OK: Success (reconnection initiated)
 *     - ESP_ERR_INVALID_STATE: Not initialized or already connecting
 *     - Other ESP error codes
 */
esp_err_t mqtt_client_manager_reconnect(void);

/**
 * @brief Check if connected to MQTT broker
 * 
 * @return true if connected, false otherwise
 */
bool mqtt_client_manager_is_connected(void);

/**
 * @brief Get current connection state
 * 
 * @return Current connection state
 */
mqtt_client_manager_state_t mqtt_client_manager_get_state(void);

/* ============================= PUBLISH OPERATIONS ============================= */

/**
 * @brief Publish message to topic
 * 
 * @param topic Message topic
 * @param payload Message payload
 * @param payload_len Payload length (0 for null-terminated string)
 * @param qos Quality of Service level
 * @param retain Retain message flag
 * @return 
 *     - Message ID: Success (positive integer)
 *     - ESP_ERR_INVALID_ARG: Invalid arguments
 *     - ESP_ERR_INVALID_STATE: Not connected
 *     - ESP_FAIL: Publish failed
 */
int mqtt_client_manager_publish(const char* topic, const char* payload, size_t payload_len, 
                               mqtt_client_manager_qos_t qos, bool retain);

/**
 * @brief Publish message with default QoS and retain settings
 * 
 * @param topic Message topic
 * @param payload Message payload
 * @return 
 *     - Message ID: Success (positive integer)
 *     - ESP_ERR_INVALID_ARG: Invalid arguments
 *     - ESP_ERR_INVALID_STATE: Not connected
 *     - ESP_FAIL: Publish failed
 */
int mqtt_client_manager_publish_simple(const char* topic, const char* payload);

/**
 * @brief Publish JSON message
 * 
 * @param topic Message topic
 * @param json_payload JSON payload (will be validated)
 * @param qos Quality of Service level
 * @param retain Retain message flag
 * @return 
 *     - Message ID: Success (positive integer)
 *     - ESP_ERR_INVALID_ARG: Invalid JSON or arguments
 *     - ESP_ERR_INVALID_STATE: Not connected
 *     - ESP_FAIL: Publish failed
 */
int mqtt_client_manager_publish_json(const char* topic, const char* json_payload, 
                                    mqtt_client_manager_qos_t qos, bool retain);

/**
 * @brief Publish formatted message
 * 
 * @param topic Message topic
 * @param qos Quality of Service level
 * @param retain Retain message flag
 * @param format Printf-style format string
 * @param ... Format arguments
 * @return 
 *     - Message ID: Success (positive integer)
 *     - ESP_ERR_INVALID_ARG: Invalid arguments or format
 *     - ESP_ERR_INVALID_STATE: Not connected
 *     - ESP_FAIL: Publish failed
 */
int mqtt_client_manager_publish_formatted(const char* topic, mqtt_client_manager_qos_t qos, 
                                         bool retain, const char* format, ...);

/* ============================= SUBSCRIBE OPERATIONS ============================= */

/**
 * @brief Subscribe to topic
 * 
 * @param topic Topic to subscribe to (supports wildcards)
 * @param qos Maximum QoS level for subscription
 * @return 
 *     - Message ID: Success (positive integer)
 *     - ESP_ERR_INVALID_ARG: Invalid topic
 *     - ESP_ERR_INVALID_STATE: Not connected
 *     - ESP_FAIL: Subscribe failed
 */
int mqtt_client_manager_subscribe(const char* topic, mqtt_client_manager_qos_t qos);

/**
 * @brief Subscribe to topic with callback
 * 
 * @param topic Topic to subscribe to (supports wildcards)
 * @param qos Maximum QoS level for subscription
 * @param callback Topic-specific message callback
 * @param user_data User data passed to callback
 * @return 
 *     - Message ID: Success (positive integer)
 *     - ESP_ERR_INVALID_ARG: Invalid topic or callback
 *     - ESP_ERR_INVALID_STATE: Not connected
 *     - ESP_ERR_NO_MEM: No memory for subscription
 *     - ESP_FAIL: Subscribe failed
 */
int mqtt_client_manager_subscribe_with_callback(const char* topic, mqtt_client_manager_qos_t qos,
                                               mqtt_client_manager_topic_callback_t callback, void* user_data);

/**
 * @brief Unsubscribe from topic
 * 
 * @param topic Topic to unsubscribe from
 * @return 
 *     - Message ID: Success (positive integer)
 *     - ESP_ERR_INVALID_ARG: Invalid topic
 *     - ESP_ERR_INVALID_STATE: Not connected
 *     - ESP_ERR_NOT_FOUND: Topic not subscribed
 *     - ESP_FAIL: Unsubscribe failed
 */
int mqtt_client_manager_unsubscribe(const char* topic);

/**
 * @brief Unsubscribe from all topics
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not connected
 */
esp_err_t mqtt_client_manager_unsubscribe_all(void);

/**
 * @brief Get list of active subscriptions
 * 
 * @param subscriptions Array to store subscription information
 * @param max_subscriptions Maximum number of subscriptions to return
 * @param actual_count Pointer to store actual number of subscriptions
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid arguments
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t mqtt_client_manager_get_subscriptions(mqtt_client_manager_subscription_t* subscriptions,
                                               size_t max_subscriptions, size_t* actual_count);

/* ============================= STATUS AND MONITORING ============================= */

/**
 * @brief Get MQTT Client Manager status
 * 
 * @param status Pointer to status structure to fill
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid argument
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t mqtt_client_manager_get_status(mqtt_client_manager_status_t* status);

/**
 * @brief Get connection statistics
 * 
 * @param stats Pointer to statistics structure to fill
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid argument
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t mqtt_client_manager_get_statistics(mqtt_client_manager_stats_t* stats);

/**
 * @brief Reset statistics counters
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t mqtt_client_manager_reset_statistics(void);

/**
 * @brief Set status message
 * 
 * @param message Status message
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid message
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t mqtt_client_manager_set_status_message(const char* message);

/**
 * @brief Get uptime in milliseconds
 * 
 * @return Uptime in milliseconds since last connection
 */
uint32_t mqtt_client_manager_get_uptime_ms(void);

/* ============================= EVENT HANDLING ============================= */

/**
 * @brief Register event callback
 * 
 * @param callback Callback function
 * @param user_data User data passed to callback
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid callback
 *     - ESP_ERR_INVALID_STATE: Not initialized
 *     - ESP_ERR_NO_MEM: No memory for callback registration
 */
esp_err_t mqtt_client_manager_register_event_callback(mqtt_client_manager_event_callback_t callback, void* user_data);

/**
 * @brief Register global message callback
 * 
 * @param callback Message callback function
 * @param user_data User data passed to callback
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid callback
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t mqtt_client_manager_register_message_callback(mqtt_client_manager_message_callback_t callback, void* user_data);

/**
 * @brief Unregister event callback
 * 
 * @param callback Callback function to unregister
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid callback
 *     - ESP_ERR_NOT_FOUND: Callback not found
 */
esp_err_t mqtt_client_manager_unregister_event_callback(mqtt_client_manager_event_callback_t callback);

/**
 * @brief Unregister message callback
 * 
 * @param callback Message callback function to unregister
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid callback
 *     - ESP_ERR_NOT_FOUND: Callback not found
 */
esp_err_t mqtt_client_manager_unregister_message_callback(mqtt_client_manager_message_callback_t callback);

/* ============================= ADVANCED FEATURES ============================= */

/**
 * @brief Enable/disable automatic reconnection
 * 
 * @param enable Enable automatic reconnection
 * @param retry_interval_ms Retry interval in milliseconds
 * @param max_attempts Maximum retry attempts (0 = infinite)
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t mqtt_client_manager_set_auto_reconnect(bool enable, uint32_t retry_interval_ms, uint8_t max_attempts);

/**
 * @brief Set SSL/TLS configuration
 * 
 * @param ssl_config SSL/TLS configuration
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid SSL configuration
 *     - ESP_ERR_INVALID_STATE: Connected (disconnect first)
 */
esp_err_t mqtt_client_manager_set_ssl_config(const mqtt_client_manager_ssl_config_t* ssl_config);

/**
 * @brief Set Last Will and Testament
 * 
 * @param will_config Will configuration
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid will configuration
 *     - ESP_ERR_INVALID_STATE: Connected (disconnect first)
 */
esp_err_t mqtt_client_manager_set_will(const mqtt_client_manager_will_config_t* will_config);

/**
 * @brief Enable/disable message queuing
 * 
 * @param enable Enable message queuing
 * @param queue_size Maximum queue size
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t mqtt_client_manager_set_message_queue(bool enable, uint16_t queue_size);

/**
 * @brief Get queued message count
 * 
 * @return Number of messages in queue
 */
uint16_t mqtt_client_manager_get_queue_count(void);

/**
 * @brief Clear message queue
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t mqtt_client_manager_clear_queue(void);

/* ============================= UTILITY FUNCTIONS ============================= */

/**
 * @brief Convert state to string
 * 
 * @param state MQTT Client Manager state
 * @return String representation of state
 */
const char* mqtt_client_manager_state_to_string(mqtt_client_manager_state_t state);

/**
 * @brief Convert QoS to string
 * 
 * @param qos QoS level
 * @return String representation of QoS
 */
const char* mqtt_client_manager_qos_to_string(mqtt_client_manager_qos_t qos);

/**
 * @brief Convert transport type to string
 * 
 * @param transport Transport type
 * @return String representation of transport
 */
const char* mqtt_client_manager_transport_to_string(mqtt_client_manager_transport_t transport);

/**
 * @brief Validate topic name
 * 
 * @param topic Topic to validate
 * @param allow_wildcards Allow wildcard characters
 * @return true if valid, false otherwise
 */
bool mqtt_client_manager_validate_topic(const char* topic, bool allow_wildcards);

/**
 * @brief Check if topic matches pattern
 * 
 * @param topic Topic to check
 * @param pattern Pattern with wildcards
 * @return true if topic matches pattern, false otherwise
 */
bool mqtt_client_manager_topic_matches(const char* topic, const char* pattern);

/**
 * @brief Validate broker URL
 * 
 * @param url URL to validate
 * @return true if valid, false otherwise
 */
bool mqtt_client_manager_validate_broker_url(const char* url);

/**
 * @brief Get suggested QoS for topic
 * 
 * Based on topic patterns and best practices.
 * 
 * @param topic Topic name
 * @return Suggested QoS level
 */
mqtt_client_manager_qos_t mqtt_client_manager_get_suggested_qos(const char* topic);

/* ============================= MESSAGE HANDLING ============================= */

/**
 * @brief Create message structure
 * 
 * @param topic Message topic
 * @param payload Message payload
 * @param payload_len Payload length
 * @param qos Quality of Service
 * @param retain Retain flag
 * @return Pointer to message structure (must be freed with mqtt_client_manager_free_message)
 */
mqtt_client_manager_message_t* mqtt_client_manager_create_message(const char* topic, const char* payload,
                                                                 size_t payload_len, mqtt_client_manager_qos_t qos, bool retain);

/**
 * @brief Free message structure
 * 
 * @param message Message to free
 */
void mqtt_client_manager_free_message(mqtt_client_manager_message_t* message);

/**
 * @brief Clone message structure
 * 
 * @param message Message to clone
 * @return Pointer to cloned message (must be freed with mqtt_client_manager_free_message)
 */
mqtt_client_manager_message_t* mqtt_client_manager_clone_message(const mqtt_client_manager_message_t* message);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_CLIENT_MANAGER_H */