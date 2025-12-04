/**
 * @file wifi_manager.h
 * @brief Comprehensive WiFi Management Library for ESP32
 * 
 * This library provides a complete WiFi management solution for ESP32 projects with:
 * - Dual mode operation (AP + STA simultaneously)
 * - Automatic fallback mechanisms
 * - Network scanning and monitoring
 * - Configuration management integration
 * - Thread-safe operations
 * - Event-driven architecture
 * 
 * Features:
 * - Simultaneous AP and STA operation (APSTA mode)
 * - Smart mode switching (APSTA → STA only when connected)
 * - Asynchronous network scanning
 * - Automatic connection retry and fallback
 * - Static IP and DHCP configuration
 * - Configuration persistence (JSON + NVS)
 * - Connection status monitoring
 * - Thread-safe operations with mutex protection
 * 
 * @version 1.0.0
 * @date 2024-11-10
 * @author ESP32 Development Team
 * 
 * @copyright Copyright (c) 2024 ESP32 Development Team
 * Licensed under the MIT License.
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================= CONSTANTS ============================= */

#define WIFI_MANAGER_VERSION "1.0.0"

/* Configuration limits */
#define WIFI_MANAGER_MAX_APS              20     /**< Maximum APs in scan results */
#define WIFI_MANAGER_SSID_MAX_LEN         32     /**< Maximum SSID length */
#define WIFI_MANAGER_PASSWORD_MAX_LEN     64     /**< Maximum password length */
#define WIFI_MANAGER_IP_MAX_LEN           16     /**< Maximum IP address string length */
#define WIFI_MANAGER_STATUS_MSG_MAX_LEN   256    /**< Maximum status message length */

/* Timeout constants */
#define WIFI_MANAGER_DEFAULT_CONNECT_TIMEOUT_MS     15000    /**< Default connection timeout */
#define WIFI_MANAGER_DEFAULT_SCAN_TIMEOUT_MS        4000     /**< Default scan timeout */
#define WIFI_MANAGER_DEFAULT_FALLBACK_TIMEOUT_MS    60000    /**< Default fallback timeout */
#define WIFI_MANAGER_MUTEX_TIMEOUT_MS               5000     /**< Mutex acquisition timeout */

/* Default configuration values */
#define WIFI_MANAGER_DEFAULT_AP_SSID        "ESP32-MCT-01"
#define WIFI_MANAGER_DEFAULT_AP_PASSWORD    "12345678"
#define WIFI_MANAGER_DEFAULT_AP_IP          "192.168.4.1"
#define WIFI_MANAGER_DEFAULT_AP_CHANNEL     1
#define WIFI_MANAGER_DEFAULT_AP_MAX_CONN    4

/* ============================= ENUMERATIONS ============================= */

/**
 * @brief WiFi Manager operation modes
 */
typedef enum {
    WIFI_MANAGER_MODE_AP_ONLY = 0,        /**< Access Point mode only */
    WIFI_MANAGER_MODE_STA_ONLY,           /**< Station mode only */
    WIFI_MANAGER_MODE_APSTA,              /**< Simultaneous AP and STA mode */
    WIFI_MANAGER_MODE_AUTO                /**< Automatic mode switching */
} wifi_manager_mode_t;

/**
 * @brief WiFi Manager states
 */
typedef enum {
    WIFI_MANAGER_STATE_UNINITIALIZED = 0, /**< Not initialized */
    WIFI_MANAGER_STATE_INITIALIZING,      /**< Initialization in progress */
    WIFI_MANAGER_STATE_AP_ONLY,           /**< AP mode active */
    WIFI_MANAGER_STATE_STA_CONNECTING,    /**< STA connecting */
    WIFI_MANAGER_STATE_STA_CONNECTED,     /**< STA connected */
    WIFI_MANAGER_STATE_STA_DISCONNECTED,  /**< STA disconnected */
    WIFI_MANAGER_STATE_APSTA_ACTIVE,      /**< Both AP and STA active */
    WIFI_MANAGER_STATE_ERROR              /**< Error state */
} wifi_manager_state_t;

/**
 * @brief Network configuration types
 */
typedef enum {
    WIFI_MANAGER_IP_DHCP = 0,             /**< Use DHCP for IP configuration */
    WIFI_MANAGER_IP_STATIC                /**< Use static IP configuration */
} wifi_manager_ip_type_t;

/**
 * @brief Event types for callbacks
 */
typedef enum {
    WIFI_MANAGER_EVENT_AP_STARTED = 0,    /**< Access Point started */
    WIFI_MANAGER_EVENT_AP_STOPPED,        /**< Access Point stopped */
    WIFI_MANAGER_EVENT_AP_CLIENT_CONNECTED,    /**< Client connected to AP */
    WIFI_MANAGER_EVENT_AP_CLIENT_DISCONNECTED, /**< Client disconnected from AP */
    WIFI_MANAGER_EVENT_STA_CONNECTING,    /**< Station connecting */
    WIFI_MANAGER_EVENT_STA_CONNECTED,     /**< Station connected */
    WIFI_MANAGER_EVENT_STA_DISCONNECTED,  /**< Station disconnected */
    WIFI_MANAGER_EVENT_STA_GOT_IP,        /**< Station got IP address */
    WIFI_MANAGER_EVENT_SCAN_STARTED,      /**< Network scan started */
    WIFI_MANAGER_EVENT_SCAN_COMPLETED,    /**< Network scan completed */
    WIFI_MANAGER_EVENT_MODE_CHANGED,      /**< WiFi mode changed */
    WIFI_MANAGER_EVENT_ERROR              /**< Error occurred */
} wifi_manager_event_t;

/* ============================= STRUCTURES ============================= */

/**
 * @brief WiFi network information
 */
typedef struct {
    char ssid[WIFI_MANAGER_SSID_MAX_LEN];     /**< Network SSID */
    char password[WIFI_MANAGER_PASSWORD_MAX_LEN]; /**< Network password */
    wifi_auth_mode_t auth_mode;               /**< Authentication mode */
    int8_t rssi;                              /**< Signal strength */
    uint8_t channel;                          /**< WiFi channel */
    bool hidden;                              /**< Hidden network flag */
} wifi_manager_network_info_t;

/**
 * @brief Access Point configuration
 */
typedef struct {
    char ssid[WIFI_MANAGER_SSID_MAX_LEN];     /**< AP SSID */
    char password[WIFI_MANAGER_PASSWORD_MAX_LEN]; /**< AP password */
    char ip[WIFI_MANAGER_IP_MAX_LEN];         /**< AP IP address */
    char netmask[WIFI_MANAGER_IP_MAX_LEN];    /**< AP netmask */
    char gateway[WIFI_MANAGER_IP_MAX_LEN];    /**< AP gateway */
    uint8_t channel;                          /**< WiFi channel */
    uint8_t max_connections;                  /**< Maximum client connections */
    wifi_auth_mode_t auth_mode;               /**< Authentication mode */
    bool ssid_hidden;                         /**< Hide SSID flag */
    uint16_t beacon_interval;                 /**< Beacon interval in ms */
} wifi_manager_ap_config_t;

/**
 * @brief Station configuration
 */
typedef struct {
    char ssid[WIFI_MANAGER_SSID_MAX_LEN];     /**< Target network SSID */
    char password[WIFI_MANAGER_PASSWORD_MAX_LEN]; /**< Network password */
    wifi_auth_mode_t threshold_auth_mode;     /**< Minimum auth mode */
    int8_t threshold_rssi;                    /**< Minimum RSSI */
    bool pmf_required;                        /**< PMF requirement */
    uint32_t connect_timeout_ms;              /**< Connection timeout */
    uint8_t max_retry;                        /**< Maximum retry attempts */
} wifi_manager_sta_config_t;

/**
 * @brief Network IP configuration
 */
typedef struct {
    wifi_manager_ip_type_t type;              /**< IP configuration type */
    char ip[WIFI_MANAGER_IP_MAX_LEN];         /**< IP address */
    char netmask[WIFI_MANAGER_IP_MAX_LEN];    /**< Network mask */
    char gateway[WIFI_MANAGER_IP_MAX_LEN];    /**< Gateway address */
    char dns_primary[WIFI_MANAGER_IP_MAX_LEN]; /**< Primary DNS server */
    char dns_secondary[WIFI_MANAGER_IP_MAX_LEN]; /**< Secondary DNS server */
} wifi_manager_ip_config_t;

/**
 * @brief Scan configuration
 */
typedef struct {
    char target_ssid[WIFI_MANAGER_SSID_MAX_LEN]; /**< Target SSID (empty for all) */
    uint8_t target_channel;                   /**< Target channel (0 for all) */
    bool show_hidden;                         /**< Show hidden networks */
    bool passive_scan;                        /**< Use passive scanning */
    uint32_t scan_timeout_ms;                 /**< Scan timeout */
    uint16_t max_results;                     /**< Maximum scan results */
} wifi_manager_scan_config_t;

/**
 * @brief WiFi Manager status
 */
typedef struct {
    wifi_manager_state_t state;               /**< Current state */
    wifi_manager_mode_t mode;                 /**< Current mode */
    bool ap_active;                           /**< AP interface active */
    bool sta_connected;                       /**< STA interface connected */
    char current_ssid[WIFI_MANAGER_SSID_MAX_LEN]; /**< Connected SSID */
    char sta_ip[WIFI_MANAGER_IP_MAX_LEN];     /**< STA IP address */
    char ap_ip[WIFI_MANAGER_IP_MAX_LEN];      /**< AP IP address */
    int8_t rssi;                              /**< Current RSSI */
    uint8_t connected_clients;                /**< Connected AP clients */
    uint32_t uptime_ms;                       /**< WiFi uptime in milliseconds */
    char status_message[WIFI_MANAGER_STATUS_MSG_MAX_LEN]; /**< Status message */
    bool scan_in_progress;                    /**< Scan operation active */
    uint16_t scan_results_count;              /**< Number of scan results */
    uint32_t last_scan_duration_ms;           /**< Last scan duration */
} wifi_manager_status_t;

/**
 * @brief WiFi Manager main configuration
 */
typedef struct {
    wifi_manager_mode_t mode;                 /**< Operation mode */
    wifi_manager_ap_config_t ap_config;       /**< AP configuration */
    wifi_manager_sta_config_t sta_config;     /**< STA configuration */
    wifi_manager_ip_config_t ip_config;       /**< IP configuration */
    wifi_manager_scan_config_t scan_config;   /**< Scan configuration */
    bool auto_connect;                        /**< Auto-connect on startup */
    bool auto_fallback;                       /**< Auto-fallback to AP */
    bool auto_mode_switch;                    /**< Auto switch APSTA → STA */
    uint32_t fallback_timeout_ms;             /**< Fallback timeout */
    uint32_t mode_switch_timeout_ms;          /**< Mode switch timeout */
    bool save_credentials;                    /**< Save credentials flag */
    bool enable_power_save;                   /**< Enable power saving */
} wifi_manager_config_t;

/**
 * @brief Event callback function type
 * 
 * @param event Event type
 * @param data Event data (can be NULL)
 * @param user_data User-provided data
 */
typedef void (*wifi_manager_event_callback_t)(wifi_manager_event_t event, void* data, void* user_data);

/**
 * @brief Scan result callback function type
 * 
 * @param results Array of scan results
 * @param count Number of results
 * @param user_data User-provided data
 */
typedef void (*wifi_manager_scan_callback_t)(const wifi_manager_network_info_t* results, uint16_t count, void* user_data);

/* ============================= INITIALIZATION ============================= */

/**
 * @brief Initialize WiFi Manager with default configuration
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Already initialized
 *     - ESP_ERR_NO_MEM: Memory allocation failed
 *     - Other ESP error codes
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Initialize WiFi Manager with custom configuration
 * 
 * @param config WiFi Manager configuration
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid configuration
 *     - ESP_ERR_INVALID_STATE: Already initialized
 *     - ESP_ERR_NO_MEM: Memory allocation failed
 *     - Other ESP error codes
 */
esp_err_t wifi_manager_init_with_config(const wifi_manager_config_t* config);

/**
 * @brief Deinitialize WiFi Manager and cleanup resources
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t wifi_manager_deinit(void);

/**
 * @brief Check if WiFi Manager is initialized
 * 
 * @return true if initialized, false otherwise
 */
bool wifi_manager_is_initialized(void);

/* ============================= CONFIGURATION ============================= */

/**
 * @brief Get default WiFi Manager configuration
 * 
 * @param config Pointer to configuration structure to fill
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid argument
 */
esp_err_t wifi_manager_get_default_config(wifi_manager_config_t* config);

/**
 * @brief Set WiFi Manager configuration
 * 
 * @param config New configuration
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid configuration
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t wifi_manager_set_config(const wifi_manager_config_t* config);

/**
 * @brief Get current WiFi Manager configuration
 * 
 * @param config Pointer to configuration structure to fill
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid argument
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t wifi_manager_get_config(wifi_manager_config_t* config);

/**
 * @brief Load configuration from storage
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_NOT_FOUND: Configuration not found
 *     - ESP_ERR_INVALID_STATE: Not initialized
 *     - Other ESP error codes
 */
esp_err_t wifi_manager_load_config(void);

/**
 * @brief Save configuration to storage
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not initialized
 *     - Other ESP error codes
 */
esp_err_t wifi_manager_save_config(void);

/**
 * @brief Reset configuration to defaults
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t wifi_manager_reset_config(void);

/* ============================= ACCESS POINT MANAGEMENT ============================= */

/**
 * @brief Start Access Point
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Invalid state for operation
 *     - ESP_ERR_WIFI_NOT_INIT: WiFi not initialized
 *     - Other ESP error codes
 */
esp_err_t wifi_manager_start_ap(void);

/**
 * @brief Stop Access Point
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Invalid state for operation
 *     - Other ESP error codes
 */
esp_err_t wifi_manager_stop_ap(void);

/**
 * @brief Configure Access Point
 * 
 * @param ap_config AP configuration
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid configuration
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t wifi_manager_set_ap_config(const wifi_manager_ap_config_t* ap_config);

/**
 * @brief Get Access Point configuration
 * 
 * @param ap_config Pointer to configuration structure to fill
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid argument
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t wifi_manager_get_ap_config(wifi_manager_ap_config_t* ap_config);

/**
 * @brief Check if Access Point is active
 * 
 * @return true if AP is active, false otherwise
 */
bool wifi_manager_is_ap_active(void);

/**
 * @brief Get number of connected clients to AP
 * 
 * @param count Pointer to store client count
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid argument
 *     - ESP_ERR_INVALID_STATE: AP not active
 */
esp_err_t wifi_manager_get_ap_client_count(uint8_t* count);

/* ============================= STATION MANAGEMENT ============================= */

/**
 * @brief Connect to WiFi network
 * 
 * @param ssid Network SSID
 * @param password Network password (can be NULL for open networks)
 * @return 
 *     - ESP_OK: Success (connection initiated)
 *     - ESP_ERR_INVALID_ARG: Invalid arguments
 *     - ESP_ERR_INVALID_STATE: Invalid state for operation
 *     - Other ESP error codes
 */
esp_err_t wifi_manager_connect(const char* ssid, const char* password);

/**
 * @brief Connect to WiFi network with custom configuration
 * 
 * @param sta_config Station configuration
 * @return 
 *     - ESP_OK: Success (connection initiated)
 *     - ESP_ERR_INVALID_ARG: Invalid configuration
 *     - ESP_ERR_INVALID_STATE: Invalid state for operation
 *     - Other ESP error codes
 */
esp_err_t wifi_manager_connect_with_config(const wifi_manager_sta_config_t* sta_config);

/**
 * @brief Disconnect from current WiFi network
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not connected
 */
esp_err_t wifi_manager_disconnect(void);

/**
 * @brief Reconnect to last saved WiFi network
 * 
 * @return 
 *     - ESP_OK: Success (connection initiated)
 *     - ESP_ERR_NOT_FOUND: No saved network configuration
 *     - ESP_ERR_INVALID_STATE: Invalid state for operation
 *     - Other ESP error codes
 */
esp_err_t wifi_manager_reconnect(void);

/**
 * @brief Check if station is connected
 * 
 * @return true if connected, false otherwise
 */
bool wifi_manager_is_sta_connected(void);

/**
 * @brief Get station connection information
 * 
 * @param network_info Pointer to network info structure to fill
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid argument
 *     - ESP_ERR_INVALID_STATE: Not connected
 */
esp_err_t wifi_manager_get_sta_info(wifi_manager_network_info_t* network_info);

/* ============================= NETWORK SCANNING ============================= */

/**
 * @brief Start network scan (blocking)
 * 
 * @param results Pointer to results array
 * @param max_results Maximum number of results
 * @param actual_count Pointer to store actual result count
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid arguments
 *     - ESP_ERR_INVALID_STATE: Scan already in progress
 *     - Other ESP error codes
 */
esp_err_t wifi_manager_scan(wifi_manager_network_info_t* results, uint16_t max_results, uint16_t* actual_count);

/**
 * @brief Start network scan (asynchronous)
 * 
 * @param callback Callback function for results
 * @param user_data User data passed to callback
 * @return 
 *     - ESP_OK: Success (scan started)
 *     - ESP_ERR_INVALID_ARG: Invalid callback
 *     - ESP_ERR_INVALID_STATE: Scan already in progress
 *     - Other ESP error codes
 */
esp_err_t wifi_manager_scan_async(wifi_manager_scan_callback_t callback, void* user_data);

/**
 * @brief Start network scan with custom configuration
 * 
 * @param scan_config Scan configuration
 * @param results Pointer to results array
 * @param max_results Maximum number of results
 * @param actual_count Pointer to store actual result count
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid arguments
 *     - ESP_ERR_INVALID_STATE: Scan already in progress
 *     - Other ESP error codes
 */
esp_err_t wifi_manager_scan_with_config(const wifi_manager_scan_config_t* scan_config,
                                       wifi_manager_network_info_t* results,
                                       uint16_t max_results,
                                       uint16_t* actual_count);

/**
 * @brief Check if scan is in progress
 * 
 * @return true if scan is in progress, false otherwise
 */
bool wifi_manager_is_scan_in_progress(void);

/**
 * @brief Get estimated time left for current scan
 * 
 * @return Estimated time left in milliseconds (0 if no scan active)
 */
uint32_t wifi_manager_get_scan_time_left_ms(void);

/**
 * @brief Get last scan results
 * 
 * @param results Pointer to results array
 * @param max_results Maximum number of results
 * @param actual_count Pointer to store actual result count
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid arguments
 *     - ESP_ERR_NOT_FOUND: No scan results available
 */
esp_err_t wifi_manager_get_scan_results(wifi_manager_network_info_t* results, uint16_t max_results, uint16_t* actual_count);

/* ============================= IP CONFIGURATION ============================= */

/**
 * @brief Set IP configuration for station interface
 * 
 * @param ip_config IP configuration
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid configuration
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t wifi_manager_set_ip_config(const wifi_manager_ip_config_t* ip_config);

/**
 * @brief Get current IP configuration
 * 
 * @param ip_config Pointer to configuration structure to fill
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid argument
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t wifi_manager_get_ip_config(wifi_manager_ip_config_t* ip_config);

/**
 * @brief Apply static IP configuration
 * 
 * @param ip IP address
 * @param netmask Network mask
 * @param gateway Gateway address
 * @param dns_primary Primary DNS server (can be NULL)
 * @param dns_secondary Secondary DNS server (can be NULL)
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid IP configuration
 *     - ESP_ERR_INVALID_STATE: Station not initialized
 *     - Other ESP error codes
 */
esp_err_t wifi_manager_apply_static_ip(const char* ip, const char* netmask, const char* gateway,
                                      const char* dns_primary, const char* dns_secondary);

/**
 * @brief Enable DHCP for station interface
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Station not initialized
 *     - Other ESP error codes
 */
esp_err_t wifi_manager_enable_dhcp(void);

/* ============================= MODE MANAGEMENT ============================= */

/**
 * @brief Set WiFi operation mode
 * 
 * @param mode WiFi operation mode
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid mode
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t wifi_manager_set_mode(wifi_manager_mode_t mode);

/**
 * @brief Get current WiFi operation mode
 * 
 * @return Current WiFi mode
 */
wifi_manager_mode_t wifi_manager_get_mode(void);

/**
 * @brief Enable automatic mode switching
 * 
 * When enabled, the manager will automatically switch from APSTA to STA mode
 * when a successful STA connection is established.
 * 
 * @param enable Enable automatic mode switching
 * @param timeout_ms Timeout for mode switch operation
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t wifi_manager_set_auto_mode_switch(bool enable, uint32_t timeout_ms);

/* ============================= STATUS AND MONITORING ============================= */

/**
 * @brief Get current WiFi Manager status
 * 
 * @param status Pointer to status structure to fill
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid argument
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t wifi_manager_get_status(wifi_manager_status_t* status);

/**
 * @brief Get current state
 * 
 * @return Current WiFi Manager state
 */
wifi_manager_state_t wifi_manager_get_state(void);

/**
 * @brief Set status message
 * 
 * @param message Status message (will be truncated if too long)
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid message
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t wifi_manager_set_status_message(const char* message);

/**
 * @brief Get uptime in milliseconds
 * 
 * @return WiFi Manager uptime in milliseconds
 */
uint32_t wifi_manager_get_uptime_ms(void);

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
esp_err_t wifi_manager_register_event_callback(wifi_manager_event_callback_t callback, void* user_data);

/**
 * @brief Unregister event callback
 * 
 * @param callback Callback function to unregister
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid callback
 *     - ESP_ERR_NOT_FOUND: Callback not found
 */
esp_err_t wifi_manager_unregister_event_callback(wifi_manager_event_callback_t callback);

/* ============================= UTILITY FUNCTIONS ============================= */

/**
 * @brief Convert WiFi auth mode to string
 * 
 * @param auth_mode Authentication mode
 * @return String representation of auth mode
 */
const char* wifi_manager_auth_mode_to_string(wifi_auth_mode_t auth_mode);

/**
 * @brief Convert WiFi Manager state to string
 * 
 * @param state WiFi Manager state
 * @return String representation of state
 */
const char* wifi_manager_state_to_string(wifi_manager_state_t state);

/**
 * @brief Convert WiFi Manager mode to string
 * 
 * @param mode WiFi Manager mode
 * @return String representation of mode
 */
const char* wifi_manager_mode_to_string(wifi_manager_mode_t mode);

/**
 * @brief Validate SSID
 * 
 * @param ssid SSID to validate
 * @return true if valid, false otherwise
 */
bool wifi_manager_validate_ssid(const char* ssid);

/**
 * @brief Validate password
 * 
 * @param password Password to validate
 * @param auth_mode Authentication mode
 * @return true if valid, false otherwise
 */
bool wifi_manager_validate_password(const char* password, wifi_auth_mode_t auth_mode);

/**
 * @brief Validate IP address
 * 
 * @param ip IP address string to validate
 * @return true if valid, false otherwise
 */
bool wifi_manager_validate_ip(const char* ip);

/* ============================= ADVANCED FEATURES ============================= */

/**
 * @brief Enable/disable automatic fallback to AP mode
 * 
 * When enabled, if STA connection fails, the manager will automatically
 * start AP mode to allow configuration.
 * 
 * @param enable Enable automatic fallback
 * @param timeout_ms Timeout before fallback (0 for immediate)
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t wifi_manager_set_auto_fallback(bool enable, uint32_t timeout_ms);

/**
 * @brief Enable/disable credential saving
 * 
 * @param enable Enable credential saving
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t wifi_manager_set_save_credentials(bool enable);

/**
 * @brief Clear saved credentials
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t wifi_manager_clear_credentials(void);

/**
 * @brief Enable/disable power saving mode
 * 
 * @param enable Enable power saving
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not initialized
 *     - Other ESP error codes
 */
esp_err_t wifi_manager_set_power_save(bool enable);

/**
 * @brief Get signal strength for current connection
 * 
 * @param rssi Pointer to store RSSI value
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid argument
 *     - ESP_ERR_INVALID_STATE: Not connected
 */
esp_err_t wifi_manager_get_rssi(int8_t* rssi);

/**
 * @brief Perform connectivity test
 * 
 * Tests connectivity by attempting to reach a known host.
 * 
 * @param host Host to test (NULL for default)
 * @param timeout_ms Test timeout
 * @return 
 *     - ESP_OK: Connectivity test passed
 *     - ESP_ERR_TIMEOUT: Test timed out
 *     - ESP_ERR_INVALID_STATE: Not connected
 *     - Other ESP error codes
 */
esp_err_t wifi_manager_test_connectivity(const char* host, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_MANAGER_H */