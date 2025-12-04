/**
 * @file wifi_manager.c
 * @brief WiFi Manager Library Implementation
 * 
 * Comprehensive WiFi management solution for ESP32 with dual mode operation,
 * automatic fallback, network scanning, and configuration management.
 * 
 * @version 1.0.0
 * @date 2024-11-10
 * @author ESP32 Development Team
 */

#include "wifi_manager.h"
#include "config_manager.h"
#include "ap_manager.h"
#include <string.h>
#include <stdio.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_ping.h>
#include <esp_mac.h>

/* ============================= CONSTANTS ============================= */

static const char *TAG = "WIFI_MANAGER";

/* Internal event bits */
#define WIFI_MANAGER_CONNECTED_BIT    BIT0
#define WIFI_MANAGER_FAIL_BIT         BIT1
#define WIFI_MANAGER_SCAN_DONE_BIT    BIT2

/* Default connectivity test host */
#define DEFAULT_CONNECTIVITY_HOST     "8.8.8.8"

/* Task stack sizes */
#define WIFI_MANAGER_TASK_STACK_SIZE        4096
#define WIFI_MANAGER_SCAN_TASK_STACK_SIZE   8192

/* ============================= INTERNAL STRUCTURES ============================= */

/**
 * @brief Event callback registration entry
 */
typedef struct wifi_manager_callback_entry {
    wifi_manager_event_callback_t callback;
    void* user_data;
    struct wifi_manager_callback_entry* next;
} wifi_manager_callback_entry_t;

/**
 * @brief Scan operation context
 */
typedef struct {
    wifi_manager_scan_callback_t callback;
    void* user_data;
    wifi_manager_network_info_t* results;
    uint16_t max_results;
    uint16_t* actual_count;
    bool async_mode;
} wifi_manager_scan_context_t;

/**
 * @brief Internal WiFi Manager context
 */
typedef struct {
    /* Initialization state */
    bool initialized;
    uint32_t init_time_ms;
    
    /* Configuration */
    wifi_manager_config_t config;
    
    /* Current state */
    wifi_manager_state_t state;
    wifi_manager_mode_t current_mode;
    
    /* Network interfaces */
    esp_netif_t* sta_netif;
    esp_netif_t* ap_netif;
    
    /* Event handling */
    esp_event_handler_instance_t wifi_event_handler;
    esp_event_handler_instance_t ip_event_handler;
    EventGroupHandle_t event_group;
    wifi_manager_callback_entry_t* callback_list;
    
    /* Synchronization */
    SemaphoreHandle_t mutex;
    SemaphoreHandle_t scan_mutex;
    
    /* Status tracking */
    char current_ssid[WIFI_MANAGER_SSID_MAX_LEN];
    char sta_ip[WIFI_MANAGER_IP_MAX_LEN];
    char ap_ip[WIFI_MANAGER_IP_MAX_LEN];
    int8_t current_rssi;
    char status_message[WIFI_MANAGER_STATUS_MSG_MAX_LEN];
    
    /* Scan results */
    wifi_manager_network_info_t* scan_results;
    uint16_t scan_results_count;
    uint16_t scan_results_capacity;
    bool scan_in_progress;
    uint32_t scan_start_time;
    uint32_t last_scan_duration;
    wifi_manager_scan_context_t scan_context;
    
    /* Connection management */
    TaskHandle_t fallback_task;
    TaskHandle_t mode_switch_task;
    uint8_t retry_count;
    
    /* Statistics */
    uint32_t connection_attempts;
    uint32_t successful_connections;
    uint32_t scan_count;
    
} wifi_manager_context_t;

/* ============================= GLOBAL VARIABLES ============================= */

static wifi_manager_context_t g_wifi_manager = {0};

/* ============================= FORWARD DECLARATIONS ============================= */

static esp_err_t wifi_manager_create_netifs(void);
static esp_err_t wifi_manager_destroy_netifs(void);
static esp_err_t wifi_manager_register_event_handlers(void);
static esp_err_t wifi_manager_unregister_event_handlers(void);
static void wifi_manager_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void wifi_manager_ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static esp_err_t wifi_manager_trigger_event(wifi_manager_event_t event, void* data);
static esp_err_t wifi_manager_change_state(wifi_manager_state_t new_state);
static esp_err_t wifi_manager_set_wifi_mode(wifi_mode_t mode);
static void wifi_manager_fallback_task(void* param);
static void wifi_manager_mode_switch_task(void* param);
static void wifi_manager_scan_task(void* param);
static esp_err_t wifi_manager_internal_scan(const wifi_manager_scan_config_t* config, bool blocking);
static esp_err_t wifi_manager_apply_ip_config(void);

/* ============================= UTILITY MACROS ============================= */

#define WIFI_MANAGER_CHECK_INITIALIZED() \
    do { \
        if (!g_wifi_manager.initialized) { \
            ESP_LOGE(TAG, "WiFi Manager not initialized"); \
            return ESP_ERR_INVALID_STATE; \
        } \
    } while(0)

#define WIFI_MANAGER_LOCK() \
    do { \
        if (xSemaphoreTake(g_wifi_manager.mutex, pdMS_TO_TICKS(WIFI_MANAGER_MUTEX_TIMEOUT_MS)) != pdTRUE) { \
            ESP_LOGE(TAG, "Failed to acquire mutex"); \
            return ESP_ERR_TIMEOUT; \
        } \
    } while(0)

#define WIFI_MANAGER_UNLOCK() \
    do { \
        xSemaphoreGive(g_wifi_manager.mutex); \
    } while(0)

/* ============================= INITIALIZATION ============================= */

esp_err_t wifi_manager_init(void) {
    wifi_manager_config_t default_config;
    esp_err_t ret = wifi_manager_get_default_config(&default_config);
    if (ret != ESP_OK) {
        return ret;
    }
    
    return wifi_manager_init_with_config(&default_config);
}

esp_err_t wifi_manager_init_with_config(const wifi_manager_config_t* config) {
    ESP_LOGI(TAG, "Initializing WiFi Manager v%s", WIFI_MANAGER_VERSION);
    
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (g_wifi_manager.initialized) {
        ESP_LOGW(TAG, "WiFi Manager already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Clear context */
    memset(&g_wifi_manager, 0, sizeof(wifi_manager_context_t));
    
    /* Create synchronization objects */
    g_wifi_manager.mutex = xSemaphoreCreateMutex();
    if (g_wifi_manager.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    g_wifi_manager.scan_mutex = xSemaphoreCreateMutex();
    if (g_wifi_manager.scan_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create scan mutex");
        vSemaphoreDelete(g_wifi_manager.mutex);
        return ESP_ERR_NO_MEM;
    }
    
    g_wifi_manager.event_group = xEventGroupCreate();
    if (g_wifi_manager.event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        vSemaphoreDelete(g_wifi_manager.mutex);
        vSemaphoreDelete(g_wifi_manager.scan_mutex);
        return ESP_ERR_NO_MEM;
    }
    
    /* Initialize configuration */
    memcpy(&g_wifi_manager.config, config, sizeof(wifi_manager_config_t));
    
    /* Allocate scan results buffer */
    g_wifi_manager.scan_results_capacity = WIFI_MANAGER_MAX_APS;
    g_wifi_manager.scan_results = (wifi_manager_network_info_t*)calloc(
        g_wifi_manager.scan_results_capacity, sizeof(wifi_manager_network_info_t));
    if (g_wifi_manager.scan_results == NULL) {
        ESP_LOGE(TAG, "Failed to allocate scan results buffer");
        vSemaphoreDelete(g_wifi_manager.mutex);
        vSemaphoreDelete(g_wifi_manager.scan_mutex);
        vEventGroupDelete(g_wifi_manager.event_group);
        return ESP_ERR_NO_MEM;
    }
    
    /* Initialize ESP-IDF networking */
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize netif: %s", esp_err_to_name(ret));
        free(g_wifi_manager.scan_results);
        vSemaphoreDelete(g_wifi_manager.mutex);
        vSemaphoreDelete(g_wifi_manager.scan_mutex);
        vEventGroupDelete(g_wifi_manager.event_group);
        return ret;
    }
    
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(ret));
        free(g_wifi_manager.scan_results);
        vSemaphoreDelete(g_wifi_manager.mutex);
        vSemaphoreDelete(g_wifi_manager.scan_mutex);
        vEventGroupDelete(g_wifi_manager.event_group);
        return ret;
    }
    
    /* Create network interfaces */
    ret = wifi_manager_create_netifs();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create network interfaces: %s", esp_err_to_name(ret));
        free(g_wifi_manager.scan_results);
        vSemaphoreDelete(g_wifi_manager.mutex);
        vSemaphoreDelete(g_wifi_manager.scan_mutex);
        vEventGroupDelete(g_wifi_manager.event_group);
        return ret;
    }
    
    /* Initialize WiFi */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    /* Optimize buffers for better performance */
    cfg.static_rx_buf_num = 16;
    cfg.dynamic_rx_buf_num = 32;
    cfg.rx_ba_win = 16;
    
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(ret));
        wifi_manager_destroy_netifs();
        free(g_wifi_manager.scan_results);
        vSemaphoreDelete(g_wifi_manager.mutex);
        vSemaphoreDelete(g_wifi_manager.scan_mutex);
        vEventGroupDelete(g_wifi_manager.event_group);
        return ret;
    }
    
    /* Register event handlers */
    ret = wifi_manager_register_event_handlers();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register event handlers: %s", esp_err_to_name(ret));
        esp_wifi_deinit();
        wifi_manager_destroy_netifs();
        free(g_wifi_manager.scan_results);
        vSemaphoreDelete(g_wifi_manager.mutex);
        vSemaphoreDelete(g_wifi_manager.scan_mutex);
        vEventGroupDelete(g_wifi_manager.event_group);
        return ret;
    }
    
    /* Set initial state */
    g_wifi_manager.state = WIFI_MANAGER_STATE_INITIALIZING;
    g_wifi_manager.current_mode = config->mode;
    g_wifi_manager.init_time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    g_wifi_manager.initialized = true;
    
    /* Apply initial IP configuration */
    strcpy(g_wifi_manager.ap_ip, config->ap_config.ip);
    
    wifi_manager_set_status_message("WiFi Manager initialized");
    
    ESP_LOGI(TAG, "WiFi Manager initialized successfully");
    ESP_LOGI(TAG, "  Mode: %s", wifi_manager_mode_to_string(config->mode));
    ESP_LOGI(TAG, "  AP SSID: %s", config->ap_config.ssid);
    ESP_LOGI(TAG, "  Auto-connect: %s", config->auto_connect ? "enabled" : "disabled");
    ESP_LOGI(TAG, "  Auto-fallback: %s", config->auto_fallback ? "enabled" : "disabled");
    
    /* Trigger initialization complete event */
    wifi_manager_trigger_event(WIFI_MANAGER_EVENT_AP_STARTED, NULL);
    
    return ESP_OK;
}

esp_err_t wifi_manager_deinit(void) {
    ESP_LOGI(TAG, "Deinitializing WiFi Manager");
    
    WIFI_MANAGER_CHECK_INITIALIZED();
    
    WIFI_MANAGER_LOCK();
    
    /* Stop any ongoing operations */
    if (g_wifi_manager.fallback_task != NULL) {
        vTaskDelete(g_wifi_manager.fallback_task);
        g_wifi_manager.fallback_task = NULL;
    }
    
    if (g_wifi_manager.mode_switch_task != NULL) {
        vTaskDelete(g_wifi_manager.mode_switch_task);
        g_wifi_manager.mode_switch_task = NULL;
    }
    
    /* Disconnect and stop WiFi */
    esp_wifi_disconnect();
    esp_wifi_stop();
    
    /* Unregister event handlers */
    wifi_manager_unregister_event_handlers();
    
    /* Deinitialize WiFi */
    esp_wifi_deinit();
    
    /* Destroy network interfaces */
    wifi_manager_destroy_netifs();
    
    /* Free scan results */
    if (g_wifi_manager.scan_results != NULL) {
        free(g_wifi_manager.scan_results);
        g_wifi_manager.scan_results = NULL;
    }
    
    /* Free callback list */
    wifi_manager_callback_entry_t* entry = g_wifi_manager.callback_list;
    while (entry != NULL) {
        wifi_manager_callback_entry_t* next = entry->next;
        free(entry);
        entry = next;
    }
    g_wifi_manager.callback_list = NULL;
    
    /* Mark as uninitialized */
    g_wifi_manager.initialized = false;
    
    WIFI_MANAGER_UNLOCK();
    
    /* Destroy synchronization objects */
    vSemaphoreDelete(g_wifi_manager.mutex);
    vSemaphoreDelete(g_wifi_manager.scan_mutex);
    vEventGroupDelete(g_wifi_manager.event_group);
    
    ESP_LOGI(TAG, "WiFi Manager deinitialized");
    
    return ESP_OK;
}

bool wifi_manager_is_initialized(void) {
    return g_wifi_manager.initialized;
}

/* ============================= CONFIGURATION ============================= */

esp_err_t wifi_manager_get_default_config(wifi_manager_config_t* config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(config, 0, sizeof(wifi_manager_config_t));
    
    /* Set default mode */
    config->mode = WIFI_MANAGER_MODE_APSTA;
    
    /* Default AP configuration */
    strcpy(config->ap_config.ssid, WIFI_MANAGER_DEFAULT_AP_SSID);
    strcpy(config->ap_config.password, WIFI_MANAGER_DEFAULT_AP_PASSWORD);
    strcpy(config->ap_config.ip, WIFI_MANAGER_DEFAULT_AP_IP);
    strcpy(config->ap_config.netmask, "255.255.255.0");
    strcpy(config->ap_config.gateway, WIFI_MANAGER_DEFAULT_AP_IP);
    config->ap_config.channel = WIFI_MANAGER_DEFAULT_AP_CHANNEL;
    config->ap_config.max_connections = WIFI_MANAGER_DEFAULT_AP_MAX_CONN;
    config->ap_config.auth_mode = WIFI_AUTH_WPA_WPA2_PSK;
    config->ap_config.ssid_hidden = false;
    config->ap_config.beacon_interval = 100;
    
    /* Default STA configuration */
    config->sta_config.threshold_auth_mode = WIFI_AUTH_WPA2_PSK;
    config->sta_config.threshold_rssi = -127;
    config->sta_config.pmf_required = false;
    config->sta_config.connect_timeout_ms = WIFI_MANAGER_DEFAULT_CONNECT_TIMEOUT_MS;
    config->sta_config.max_retry = 3;
    
    /* Default IP configuration */
    config->ip_config.type = WIFI_MANAGER_IP_DHCP;
    
    /* Default scan configuration */
    config->scan_config.show_hidden = true;
    config->scan_config.passive_scan = false;
    config->scan_config.scan_timeout_ms = WIFI_MANAGER_DEFAULT_SCAN_TIMEOUT_MS;
    config->scan_config.max_results = WIFI_MANAGER_MAX_APS;
    
    /* Default behavior */
    config->auto_connect = true;
    config->auto_fallback = true;
    config->auto_mode_switch = true;
    config->fallback_timeout_ms = WIFI_MANAGER_DEFAULT_FALLBACK_TIMEOUT_MS;
    config->mode_switch_timeout_ms = WIFI_MANAGER_DEFAULT_CONNECT_TIMEOUT_MS;
    config->save_credentials = true;
    config->enable_power_save = false;
    
    return ESP_OK;
}

esp_err_t wifi_manager_set_config(const wifi_manager_config_t* config) {
    WIFI_MANAGER_CHECK_INITIALIZED();
    
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    WIFI_MANAGER_LOCK();
    
    /* Validate configuration */
    if (!wifi_manager_validate_ssid(config->ap_config.ssid)) {
        WIFI_MANAGER_UNLOCK();
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!wifi_manager_validate_password(config->ap_config.password, config->ap_config.auth_mode)) {
        WIFI_MANAGER_UNLOCK();
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!wifi_manager_validate_ip(config->ap_config.ip)) {
        WIFI_MANAGER_UNLOCK();
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Update configuration */
    memcpy(&g_wifi_manager.config, config, sizeof(wifi_manager_config_t));
    
    /* Update AP IP */
    strcpy(g_wifi_manager.ap_ip, config->ap_config.ip);
    
    WIFI_MANAGER_UNLOCK();
    
    wifi_manager_set_status_message("Configuration updated");
    
    ESP_LOGI(TAG, "Configuration updated");
    
    return ESP_OK;
}

esp_err_t wifi_manager_get_config(wifi_manager_config_t* config) {
    WIFI_MANAGER_CHECK_INITIALIZED();
    
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    WIFI_MANAGER_LOCK();
    memcpy(config, &g_wifi_manager.config, sizeof(wifi_manager_config_t));
    WIFI_MANAGER_UNLOCK();
    
    return ESP_OK;
}

esp_err_t wifi_manager_load_config(void) {
    WIFI_MANAGER_CHECK_INITIALIZED();
    
    ESP_LOGI(TAG, "Loading configuration from storage");
    
    /* Try to load AP configuration */
    ap_config_t ap_config;
    if (load_ap_config(&ap_config) == ESP_OK) {
        ESP_LOGI(TAG, "Loaded AP configuration from storage");
        strcpy(g_wifi_manager.config.ap_config.ssid, ap_config.ssid);
        strcpy(g_wifi_manager.config.ap_config.password, ap_config.password);
        strcpy(g_wifi_manager.config.ap_config.ip, ap_config.ip);
        strcpy(g_wifi_manager.ap_ip, ap_config.ip);
    }
    
    /* Try to load STA configuration */
    sta_config_t sta_config;
    if (load_sta_config(&sta_config) == ESP_OK) {
        ESP_LOGI(TAG, "Loaded STA configuration from storage");
        strcpy(g_wifi_manager.config.sta_config.ssid, sta_config.ssid);
        strcpy(g_wifi_manager.config.sta_config.password, sta_config.password);
    }
    
    /* Try to load network configuration */
    network_config_t net_config;
    if (load_network_config(&net_config) == ESP_OK) {
        ESP_LOGI(TAG, "Loaded network configuration from storage");
        if (strlen(net_config.ip) > 0) {
            g_wifi_manager.config.ip_config.type = WIFI_MANAGER_IP_STATIC;
            strcpy(g_wifi_manager.config.ip_config.ip, net_config.ip);
            strcpy(g_wifi_manager.config.ip_config.netmask, net_config.mask);
            strcpy(g_wifi_manager.config.ip_config.gateway, net_config.gateway);
            strcpy(g_wifi_manager.config.ip_config.dns_primary, net_config.dns);
        } else {
            g_wifi_manager.config.ip_config.type = WIFI_MANAGER_IP_DHCP;
        }
    }
    
    return ESP_OK;
}

esp_err_t wifi_manager_save_config(void) {
    WIFI_MANAGER_CHECK_INITIALIZED();
    
    if (!g_wifi_manager.config.save_credentials) {
        ESP_LOGI(TAG, "Credential saving disabled, skipping save");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Saving configuration to storage");
    
    /* Save AP configuration */
    ap_config_t ap_config;
    strcpy(ap_config.ssid, g_wifi_manager.config.ap_config.ssid);
    strcpy(ap_config.password, g_wifi_manager.config.ap_config.password);
    strcpy(ap_config.ip, g_wifi_manager.config.ap_config.ip);
    
    esp_err_t ret = save_ap_config(&ap_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save AP config: %s", esp_err_to_name(ret));
    }
    
    /* Save STA configuration if we have credentials */
    if (strlen(g_wifi_manager.config.sta_config.ssid) > 0) {
        sta_config_t sta_config;
        strcpy(sta_config.ssid, g_wifi_manager.config.sta_config.ssid);
        strcpy(sta_config.password, g_wifi_manager.config.sta_config.password);
        
        ret = save_sta_config(&sta_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save STA config: %s", esp_err_to_name(ret));
        }
    }
    
    /* Save network configuration if using static IP */
    if (g_wifi_manager.config.ip_config.type == WIFI_MANAGER_IP_STATIC) {
        network_config_t net_config;
        strcpy(net_config.ip, g_wifi_manager.config.ip_config.ip);
        strcpy(net_config.mask, g_wifi_manager.config.ip_config.netmask);
        strcpy(net_config.gateway, g_wifi_manager.config.ip_config.gateway);
        strcpy(net_config.dns, g_wifi_manager.config.ip_config.dns_primary);
        
        ret = save_network_config(&net_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save network config: %s", esp_err_to_name(ret));
        }
    }
    
    return ESP_OK;
}

esp_err_t wifi_manager_reset_config(void) {
    WIFI_MANAGER_CHECK_INITIALIZED();
    
    ESP_LOGI(TAG, "Resetting configuration to defaults");
    
    wifi_manager_config_t default_config;
    esp_err_t ret = wifi_manager_get_default_config(&default_config);
    if (ret != ESP_OK) {
        return ret;
    }
    
    return wifi_manager_set_config(&default_config);
}

/* ============================= INTERNAL HELPER FUNCTIONS ============================= */

static esp_err_t wifi_manager_create_netifs(void) {
    g_wifi_manager.sta_netif = esp_netif_create_default_wifi_sta();
    if (g_wifi_manager.sta_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create STA netif");
        return ESP_FAIL;
    }
    
    g_wifi_manager.ap_netif = esp_netif_create_default_wifi_ap();
    if (g_wifi_manager.ap_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create AP netif");
        esp_netif_destroy(g_wifi_manager.sta_netif);
        g_wifi_manager.sta_netif = NULL;
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

static esp_err_t wifi_manager_destroy_netifs(void) {
    if (g_wifi_manager.ap_netif != NULL) {
        esp_netif_destroy(g_wifi_manager.ap_netif);
        g_wifi_manager.ap_netif = NULL;
    }
    
    if (g_wifi_manager.sta_netif != NULL) {
        esp_netif_destroy(g_wifi_manager.sta_netif);
        g_wifi_manager.sta_netif = NULL;
    }
    
    return ESP_OK;
}

static esp_err_t wifi_manager_register_event_handlers(void) {
    esp_err_t ret;
    
    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            &wifi_manager_event_handler, NULL,
                                            &g_wifi_manager.wifi_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WiFi event handler: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            &wifi_manager_ip_event_handler, NULL,
                                            &g_wifi_manager.ip_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP event handler: %s", esp_err_to_name(ret));
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             g_wifi_manager.wifi_event_handler);
        return ret;
    }
    
    return ESP_OK;
}

static esp_err_t wifi_manager_unregister_event_handlers(void) {
    if (g_wifi_manager.wifi_event_handler != NULL) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             g_wifi_manager.wifi_event_handler);
        g_wifi_manager.wifi_event_handler = NULL;
    }
    
    if (g_wifi_manager.ip_event_handler != NULL) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             g_wifi_manager.ip_event_handler);
        g_wifi_manager.ip_event_handler = NULL;
    }
    
    return ESP_OK;
}

/* ============================= EVENT HANDLERS ============================= */

static void wifi_manager_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "Access Point started");
                wifi_manager_change_state(WIFI_MANAGER_STATE_AP_ONLY);
                wifi_manager_trigger_event(WIFI_MANAGER_EVENT_AP_STARTED, NULL);
                break;
                
            case WIFI_EVENT_AP_STOP:
                ESP_LOGI(TAG, "Access Point stopped");
                wifi_manager_trigger_event(WIFI_MANAGER_EVENT_AP_STOPPED, NULL);
                break;
                
            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
                ESP_LOGI(TAG, "Client connected to AP: %02x:%02x:%02x:%02x:%02x:%02x", 
                         event->mac[0], event->mac[1], event->mac[2], 
                         event->mac[3], event->mac[4], event->mac[5]);
                wifi_manager_trigger_event(WIFI_MANAGER_EVENT_AP_CLIENT_CONNECTED, event);
                break;
            }
            
            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
                ESP_LOGI(TAG, "Client disconnected from AP: %02x:%02x:%02x:%02x:%02x:%02x", 
                         event->mac[0], event->mac[1], event->mac[2], 
                         event->mac[3], event->mac[4], event->mac[5]);
                wifi_manager_trigger_event(WIFI_MANAGER_EVENT_AP_CLIENT_DISCONNECTED, event);
                break;
            }
            
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "Station started");
                break;
                
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "Station connected");
                wifi_manager_change_state(WIFI_MANAGER_STATE_STA_CONNECTED);
                wifi_manager_trigger_event(WIFI_MANAGER_EVENT_STA_CONNECTED, NULL);
                xEventGroupSetBits(g_wifi_manager.event_group, WIFI_MANAGER_CONNECTED_BIT);
                break;
                
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
                ESP_LOGI(TAG, "Station disconnected, reason: %d", event->reason);
                
                /* Clear connection info */
                memset(g_wifi_manager.current_ssid, 0, sizeof(g_wifi_manager.current_ssid));
                memset(g_wifi_manager.sta_ip, 0, sizeof(g_wifi_manager.sta_ip));
                g_wifi_manager.current_rssi = 0;
                
                wifi_manager_change_state(WIFI_MANAGER_STATE_STA_DISCONNECTED);
                wifi_manager_trigger_event(WIFI_MANAGER_EVENT_STA_DISCONNECTED, event);
                
                xEventGroupSetBits(g_wifi_manager.event_group, WIFI_MANAGER_FAIL_BIT);
                break;
            }
            
            case WIFI_EVENT_SCAN_DONE:
                ESP_LOGI(TAG, "WiFi scan completed");
                xEventGroupSetBits(g_wifi_manager.event_group, WIFI_MANAGER_SCAN_DONE_BIT);
                break;
        }
    }
}

static void wifi_manager_ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        
        /* Format IP address */
        snprintf(g_wifi_manager.sta_ip, sizeof(g_wifi_manager.sta_ip), 
                "%lu.%lu.%lu.%lu",
                (unsigned long)((event->ip_info.ip.addr >> 0) & 0xFF),
                (unsigned long)((event->ip_info.ip.addr >> 8) & 0xFF),
                (unsigned long)((event->ip_info.ip.addr >> 16) & 0xFF),
                (unsigned long)((event->ip_info.ip.addr >> 24) & 0xFF));
        
        /* Get AP info if available */
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            strncpy(g_wifi_manager.current_ssid, (char*)ap_info.ssid, 
                   sizeof(g_wifi_manager.current_ssid) - 1);
            g_wifi_manager.current_rssi = ap_info.rssi;
        }
        
        ESP_LOGI(TAG, "Station got IP: %s, connected to: %s", 
                g_wifi_manager.sta_ip, g_wifi_manager.current_ssid);
        
        char status_msg[WIFI_MANAGER_STATUS_MSG_MAX_LEN];
        snprintf(status_msg, sizeof(status_msg), "Connected! IP: %s", g_wifi_manager.sta_ip);
        wifi_manager_set_status_message(status_msg);
        
        /* Update statistics */
        g_wifi_manager.successful_connections++;
        
        wifi_manager_trigger_event(WIFI_MANAGER_EVENT_STA_GOT_IP, event);
    }
}

static esp_err_t wifi_manager_trigger_event(wifi_manager_event_t event, void* data) {
    /* Call registered callbacks */
    wifi_manager_callback_entry_t* entry = g_wifi_manager.callback_list;
    while (entry != NULL) {
        if (entry->callback != NULL) {
            entry->callback(event, data, entry->user_data);
        }
        entry = entry->next;
    }
    
    return ESP_OK;
}

static esp_err_t wifi_manager_change_state(wifi_manager_state_t new_state) {
    if (g_wifi_manager.state != new_state) {
        ESP_LOGI(TAG, "State change: %s -> %s", 
                wifi_manager_state_to_string(g_wifi_manager.state),
                wifi_manager_state_to_string(new_state));
        g_wifi_manager.state = new_state;
    }
    
    return ESP_OK;
}

static esp_err_t wifi_manager_set_wifi_mode(wifi_mode_t mode) {
    esp_err_t ret = esp_wifi_set_mode(mode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Update current mode */
    switch (mode) {
        case WIFI_MODE_AP:
            g_wifi_manager.current_mode = WIFI_MANAGER_MODE_AP_ONLY;
            break;
        case WIFI_MODE_STA:
            g_wifi_manager.current_mode = WIFI_MANAGER_MODE_STA_ONLY;
            break;
        case WIFI_MODE_APSTA:
            g_wifi_manager.current_mode = WIFI_MANAGER_MODE_APSTA;
            break;
        default:
            break;
    }
    
    wifi_manager_trigger_event(WIFI_MANAGER_EVENT_MODE_CHANGED, &mode);
    
    return ESP_OK;
}

/* ============================= ACCESS POINT MANAGEMENT ============================= */

esp_err_t wifi_manager_start_ap(void) {
    WIFI_MANAGER_CHECK_INITIALIZED();
    
    ESP_LOGI(TAG, "Starting Access Point");
    
    /* Configure WiFi mode */
    wifi_mode_t target_mode = (g_wifi_manager.current_mode == WIFI_MANAGER_MODE_AP_ONLY) ? 
                              WIFI_MODE_AP : WIFI_MODE_APSTA;
    
    esp_err_t ret = wifi_manager_set_wifi_mode(target_mode);
    if (ret != ESP_OK) {
        return ret;
    }
    
    /* Configure AP */
    wifi_config_t ap_config = {0};
    strncpy((char*)ap_config.ap.ssid, g_wifi_manager.config.ap_config.ssid, 
           sizeof(ap_config.ap.ssid) - 1);
    strncpy((char*)ap_config.ap.password, g_wifi_manager.config.ap_config.password, 
           sizeof(ap_config.ap.password) - 1);
    
    ap_config.ap.ssid_len = strlen(g_wifi_manager.config.ap_config.ssid);
    ap_config.ap.channel = g_wifi_manager.config.ap_config.channel;
    ap_config.ap.max_connection = g_wifi_manager.config.ap_config.max_connections;
    ap_config.ap.authmode = g_wifi_manager.config.ap_config.auth_mode;
    ap_config.ap.beacon_interval = g_wifi_manager.config.ap_config.beacon_interval;
    
    ret = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure AP: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Start WiFi */
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(ret));
        return ret;
    }
    
    wifi_manager_set_status_message("Access Point started");
    
    return ESP_OK;
}

esp_err_t wifi_manager_stop_ap(void) {
    WIFI_MANAGER_CHECK_INITIALIZED();
    
    ESP_LOGI(TAG, "Stopping Access Point");
    
    /* Change mode to STA only if we're in dual mode */
    if (g_wifi_manager.current_mode == WIFI_MANAGER_MODE_APSTA) {
        esp_err_t ret = wifi_manager_set_wifi_mode(WIFI_MODE_STA);
        if (ret != ESP_OK) {
            return ret;
        }
    } else {
        /* Stop WiFi completely if AP only */
        esp_err_t ret = esp_wifi_stop();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop WiFi: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    
    wifi_manager_set_status_message("Access Point stopped");
    
    return ESP_OK;
}

bool wifi_manager_is_ap_active(void) {
    if (!g_wifi_manager.initialized) {
        return false;
    }
    
    wifi_mode_t mode;
    esp_err_t ret = esp_wifi_get_mode(&mode);
    if (ret != ESP_OK) {
        return false;
    }
    
    return (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA);
}

/* ============================= STATION MANAGEMENT ============================= */

esp_err_t wifi_manager_connect(const char* ssid, const char* password) {
    WIFI_MANAGER_CHECK_INITIALIZED();
    
    if (ssid == NULL || strlen(ssid) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Connecting to WiFi network: %s", ssid);
    
    /* Update configuration */
    strncpy(g_wifi_manager.config.sta_config.ssid, ssid, 
           sizeof(g_wifi_manager.config.sta_config.ssid) - 1);
    
    if (password != NULL) {
        strncpy(g_wifi_manager.config.sta_config.password, password, 
               sizeof(g_wifi_manager.config.sta_config.password) - 1);
    } else {
        g_wifi_manager.config.sta_config.password[0] = '\0';
    }
    
    /* Set WiFi mode */
    wifi_mode_t target_mode = (g_wifi_manager.current_mode == WIFI_MANAGER_MODE_STA_ONLY) ? 
                              WIFI_MODE_STA : WIFI_MODE_APSTA;
    
    esp_err_t ret = wifi_manager_set_wifi_mode(target_mode);
    if (ret != ESP_OK) {
        return ret;
    }
    
    /* Configure station */
    wifi_config_t sta_config = {0};
    strncpy((char*)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
    if (password != NULL) {
        strncpy((char*)sta_config.sta.password, password, sizeof(sta_config.sta.password) - 1);
    }
    
    sta_config.sta.threshold.authmode = g_wifi_manager.config.sta_config.threshold_auth_mode;
    sta_config.sta.threshold.rssi = g_wifi_manager.config.sta_config.threshold_rssi;
    
    ret = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure STA: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Start WiFi if not already started */
    ret = esp_wifi_start();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_STOPPED) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Apply IP configuration if needed */
    wifi_manager_apply_ip_config();
    
    /* Start connection */
    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initiate connection: %s", esp_err_to_name(ret));
        return ret;
    }
    
    wifi_manager_change_state(WIFI_MANAGER_STATE_STA_CONNECTING);
    wifi_manager_set_status_message("Connecting to WiFi...");
    wifi_manager_trigger_event(WIFI_MANAGER_EVENT_STA_CONNECTING, NULL);
    
    /* Update statistics */
    g_wifi_manager.connection_attempts++;
    
    /* Save credentials if enabled */
    if (g_wifi_manager.config.save_credentials) {
        wifi_manager_save_config();
    }
    
    return ESP_OK;
}

esp_err_t wifi_manager_disconnect(void) {
    WIFI_MANAGER_CHECK_INITIALIZED();
    
    ESP_LOGI(TAG, "Disconnecting from WiFi");
    
    esp_err_t ret = esp_wifi_disconnect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disconnect: %s", esp_err_to_name(ret));
        return ret;
    }
    
    wifi_manager_set_status_message("Disconnected");
    
    return ESP_OK;
}

bool wifi_manager_is_sta_connected(void) {
    if (!g_wifi_manager.initialized) {
        return false;
    }
    
    return (g_wifi_manager.state == WIFI_MANAGER_STATE_STA_CONNECTED) &&
           (strlen(g_wifi_manager.sta_ip) > 0);
}

/* ============================= STATUS AND MONITORING ============================= */

esp_err_t wifi_manager_get_status(wifi_manager_status_t* status) {
    WIFI_MANAGER_CHECK_INITIALIZED();
    
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    WIFI_MANAGER_LOCK();
    
    memset(status, 0, sizeof(wifi_manager_status_t));
    
    status->state = g_wifi_manager.state;
    status->mode = g_wifi_manager.current_mode;
    status->ap_active = wifi_manager_is_ap_active();
    status->sta_connected = wifi_manager_is_sta_connected();
    
    strncpy(status->current_ssid, g_wifi_manager.current_ssid, 
           sizeof(status->current_ssid) - 1);
    strncpy(status->sta_ip, g_wifi_manager.sta_ip, 
           sizeof(status->sta_ip) - 1);
    strncpy(status->ap_ip, g_wifi_manager.ap_ip, 
           sizeof(status->ap_ip) - 1);
    
    status->rssi = g_wifi_manager.current_rssi;
    status->uptime_ms = (xTaskGetTickCount() * portTICK_PERIOD_MS) - g_wifi_manager.init_time_ms;
    
    strncpy(status->status_message, g_wifi_manager.status_message, 
           sizeof(status->status_message) - 1);
    
    status->scan_in_progress = g_wifi_manager.scan_in_progress;
    status->scan_results_count = g_wifi_manager.scan_results_count;
    status->last_scan_duration_ms = g_wifi_manager.last_scan_duration;
    
    WIFI_MANAGER_UNLOCK();
    
    return ESP_OK;
}

wifi_manager_state_t wifi_manager_get_state(void) {
    return g_wifi_manager.state;
}

esp_err_t wifi_manager_set_status_message(const char* message) {
    WIFI_MANAGER_CHECK_INITIALIZED();
    
    if (message == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    WIFI_MANAGER_LOCK();
    strncpy(g_wifi_manager.status_message, message, 
           sizeof(g_wifi_manager.status_message) - 1);
    g_wifi_manager.status_message[sizeof(g_wifi_manager.status_message) - 1] = '\0';
    WIFI_MANAGER_UNLOCK();
    
    return ESP_OK;
}

uint32_t wifi_manager_get_uptime_ms(void) {
    if (!g_wifi_manager.initialized) {
        return 0;
    }
    
    return (xTaskGetTickCount() * portTICK_PERIOD_MS) - g_wifi_manager.init_time_ms;
}

/* ============================= UTILITY FUNCTIONS ============================= */

const char* wifi_manager_state_to_string(wifi_manager_state_t state) {
    switch (state) {
        case WIFI_MANAGER_STATE_UNINITIALIZED: return "Uninitialized";
        case WIFI_MANAGER_STATE_INITIALIZING: return "Initializing";
        case WIFI_MANAGER_STATE_AP_ONLY: return "AP Only";
        case WIFI_MANAGER_STATE_STA_CONNECTING: return "STA Connecting";
        case WIFI_MANAGER_STATE_STA_CONNECTED: return "STA Connected";
        case WIFI_MANAGER_STATE_STA_DISCONNECTED: return "STA Disconnected";
        case WIFI_MANAGER_STATE_APSTA_ACTIVE: return "AP+STA Active";
        case WIFI_MANAGER_STATE_ERROR: return "Error";
        default: return "Unknown";
    }
}

const char* wifi_manager_mode_to_string(wifi_manager_mode_t mode) {
    switch (mode) {
        case WIFI_MANAGER_MODE_AP_ONLY: return "AP Only";
        case WIFI_MANAGER_MODE_STA_ONLY: return "STA Only";
        case WIFI_MANAGER_MODE_APSTA: return "AP+STA";
        case WIFI_MANAGER_MODE_AUTO: return "Auto";
        default: return "Unknown";
    }
}

const char* wifi_manager_auth_mode_to_string(wifi_auth_mode_t auth_mode) {
    switch (auth_mode) {
        case WIFI_AUTH_OPEN: return "Open";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA PSK";
        case WIFI_AUTH_WPA2_PSK: return "WPA2 PSK";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2 PSK";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2 Enterprise";
        case WIFI_AUTH_WPA3_PSK: return "WPA3 PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3 PSK";
        default: return "Unknown";
    }
}

bool wifi_manager_validate_ssid(const char* ssid) {
    if (ssid == NULL) {
        return false;
    }
    
    size_t len = strlen(ssid);
    return (len > 0 && len < WIFI_MANAGER_SSID_MAX_LEN);
}

bool wifi_manager_validate_password(const char* password, wifi_auth_mode_t auth_mode) {
    if (auth_mode == WIFI_AUTH_OPEN) {
        return true; /* No password needed for open networks */
    }
    
    if (password == NULL) {
        return false;
    }
    
    size_t len = strlen(password);
    
    /* WPA/WPA2/WPA3 require at least 8 characters */
    if (auth_mode == WIFI_AUTH_WPA_PSK || 
        auth_mode == WIFI_AUTH_WPA2_PSK || 
        auth_mode == WIFI_AUTH_WPA_WPA2_PSK ||
        auth_mode == WIFI_AUTH_WPA3_PSK ||
        auth_mode == WIFI_AUTH_WPA2_WPA3_PSK) {
        return (len >= 8 && len < WIFI_MANAGER_PASSWORD_MAX_LEN);
    }
    
    /* WEP can have various lengths */
    if (auth_mode == WIFI_AUTH_WEP) {
        return (len > 0 && len < WIFI_MANAGER_PASSWORD_MAX_LEN);
    }
    
    return (len > 0 && len < WIFI_MANAGER_PASSWORD_MAX_LEN);
}

bool wifi_manager_validate_ip(const char* ip) {
    if (ip == NULL) {
        return false;
    }
    
    /* Simple IP validation - check format x.x.x.x */
    int a, b, c, d;
    int result = sscanf(ip, "%d.%d.%d.%d", &a, &b, &c, &d);
    
    if (result != 4) {
        return false;
    }
    
    return (a >= 0 && a <= 255 && b >= 0 && b <= 255 && 
            c >= 0 && c <= 255 && d >= 0 && d <= 255);
}

static esp_err_t wifi_manager_apply_ip_config(void) {
    if (g_wifi_manager.config.ip_config.type == WIFI_MANAGER_IP_STATIC) {
        ESP_LOGI(TAG, "Applying static IP configuration");
        
        esp_netif_dhcpc_stop(g_wifi_manager.sta_netif);
        
        esp_netif_ip_info_t ip_info = {0};
        
        if (esp_netif_str_to_ip4(g_wifi_manager.config.ip_config.ip, &ip_info.ip) != ESP_OK ||
            esp_netif_str_to_ip4(g_wifi_manager.config.ip_config.netmask, &ip_info.netmask) != ESP_OK ||
            esp_netif_str_to_ip4(g_wifi_manager.config.ip_config.gateway, &ip_info.gw) != ESP_OK) {
            ESP_LOGE(TAG, "Invalid static IP configuration");
            return ESP_ERR_INVALID_ARG;
        }
        
        esp_err_t ret = esp_netif_set_ip_info(g_wifi_manager.sta_netif, &ip_info);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set static IP: %s", esp_err_to_name(ret));
            return ret;
        }
        
        /* Set DNS if provided */
        if (strlen(g_wifi_manager.config.ip_config.dns_primary) > 0) {
            esp_netif_dns_info_t dns_info = {0};
            if (esp_netif_str_to_ip4(g_wifi_manager.config.ip_config.dns_primary, 
                                   &dns_info.ip.u_addr.ip4) == ESP_OK) {
                esp_netif_set_dns_info(g_wifi_manager.sta_netif, ESP_NETIF_DNS_MAIN, &dns_info);
            }
        }
        
        ESP_LOGI(TAG, "Static IP configuration applied successfully");
    } else {
        ESP_LOGI(TAG, "Using DHCP for IP configuration");
        esp_netif_dhcpc_start(g_wifi_manager.sta_netif);
    }
    
    return ESP_OK;
}

/* ============================= NETWORK SCANNING ============================= */

esp_err_t wifi_manager_scan(wifi_manager_network_info_t* results, uint16_t max_results, uint16_t* actual_count) {
    WIFI_MANAGER_CHECK_INITIALIZED();
    
    if (results == NULL || actual_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (g_wifi_manager.scan_in_progress) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting WiFi scan (blocking)");
    
    g_wifi_manager.scan_in_progress = true;
    g_wifi_manager.scan_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = g_wifi_manager.config.scan_config.target_channel,
        .show_hidden = g_wifi_manager.config.scan_config.show_hidden
    };
    
    wifi_mode_t current_mode;
    esp_wifi_get_mode(&current_mode);
    
    if (current_mode != WIFI_MODE_STA && current_mode != WIFI_MODE_APSTA) {
        wifi_manager_set_wifi_mode(WIFI_MODE_APSTA);
    }
    
    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start scan: %s", esp_err_to_name(ret));
        g_wifi_manager.scan_in_progress = false;
        return ret;
    }
    
    uint16_t scan_count = 0;
    ret = esp_wifi_scan_get_ap_num(&scan_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get AP count: %s", esp_err_to_name(ret));
        g_wifi_manager.scan_in_progress = false;
        return ret;
    }
    
    if (scan_count > max_results) {
        scan_count = max_results;
    }
    
    if (scan_count > 0) {
        wifi_ap_record_t* ap_records = (wifi_ap_record_t*)malloc(scan_count * sizeof(wifi_ap_record_t));
        if (ap_records == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for scan results");
            g_wifi_manager.scan_in_progress = false;
            return ESP_ERR_NO_MEM;
        }
        
        ret = esp_wifi_scan_get_ap_records(&scan_count, ap_records);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get AP records: %s", esp_err_to_name(ret));
            free(ap_records);
            g_wifi_manager.scan_in_progress = false;
            return ret;
        }
        
        for (int i = 0; i < scan_count; i++) {
            memset(&results[i], 0, sizeof(wifi_manager_network_info_t));
            strncpy(results[i].ssid, (char*)ap_records[i].ssid, sizeof(results[i].ssid) - 1);
            results[i].auth_mode = ap_records[i].authmode;
            results[i].rssi = ap_records[i].rssi;
            results[i].channel = ap_records[i].primary;
        }
        
        if (xSemaphoreTake(g_wifi_manager.scan_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (scan_count <= g_wifi_manager.scan_results_capacity) {
                for (int i = 0; i < scan_count; i++) {
                    memcpy(&g_wifi_manager.scan_results[i], &results[i], sizeof(wifi_manager_network_info_t));
                }
                g_wifi_manager.scan_results_count = scan_count;
            }
            xSemaphoreGive(g_wifi_manager.scan_mutex);
        }
        
        free(ap_records);
    }
    
    *actual_count = scan_count;
    
    uint32_t end_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    g_wifi_manager.last_scan_duration = end_time - g_wifi_manager.scan_start_time;
    g_wifi_manager.scan_in_progress = false;
    g_wifi_manager.scan_count++;
    
    ESP_LOGI(TAG, "Scan completed: %d networks found in %lu ms", scan_count, g_wifi_manager.last_scan_duration);
    
    wifi_manager_trigger_event(WIFI_MANAGER_EVENT_SCAN_COMPLETED, &scan_count);
    
    return ESP_OK;
}

bool wifi_manager_is_scan_in_progress(void) {
    return g_wifi_manager.scan_in_progress;
}

esp_err_t wifi_manager_scan_async(wifi_manager_scan_callback_t callback, void* user_data) {
    WIFI_MANAGER_CHECK_INITIALIZED();
    
    if (callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (g_wifi_manager.scan_in_progress) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting WiFi scan (asynchronous)");
    
    g_wifi_manager.scan_context.callback = callback;
    g_wifi_manager.scan_context.user_data = user_data;
    g_wifi_manager.scan_context.async_mode = true;
    
    BaseType_t task_ret = xTaskCreate(wifi_manager_scan_task, "wifi_scan", WIFI_MANAGER_SCAN_TASK_STACK_SIZE, NULL, 5, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create scan task");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

static void wifi_manager_scan_task(void* param) {
    ESP_LOGI(TAG, "Scan task started");
    
    wifi_manager_network_info_t* results = (wifi_manager_network_info_t*)malloc(WIFI_MANAGER_MAX_APS * sizeof(wifi_manager_network_info_t));
    
    if (results == NULL) {
        ESP_LOGE(TAG, "Failed to allocate scan results memory");
        g_wifi_manager.scan_in_progress = false;
        vTaskDelete(NULL);
        return;
    }
    
    uint16_t actual_count = 0;
    esp_err_t ret = wifi_manager_scan(results, WIFI_MANAGER_MAX_APS, &actual_count);
    
    if (g_wifi_manager.scan_context.callback != NULL) {
        g_wifi_manager.scan_context.callback(results, actual_count, g_wifi_manager.scan_context.user_data);
    }
    
    free(results);
    memset(&g_wifi_manager.scan_context, 0, sizeof(wifi_manager_scan_context_t));
    
    ESP_LOGI(TAG, "Scan task completed");
    vTaskDelete(NULL);
}

/* ============================= EVENT HANDLING ============================= */

esp_err_t wifi_manager_register_event_callback(wifi_manager_event_callback_t callback, void* user_data) {
    WIFI_MANAGER_CHECK_INITIALIZED();
    
    if (callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    wifi_manager_callback_entry_t* entry = (wifi_manager_callback_entry_t*)malloc(sizeof(wifi_manager_callback_entry_t));
    if (entry == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    entry->callback = callback;
    entry->user_data = user_data;
    entry->next = NULL;
    
    WIFI_MANAGER_LOCK();
    
    if (g_wifi_manager.callback_list == NULL) {
        g_wifi_manager.callback_list = entry;
    } else {
        wifi_manager_callback_entry_t* current = g_wifi_manager.callback_list;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = entry;
    }
    
    WIFI_MANAGER_UNLOCK();
    
    ESP_LOGI(TAG, "Event callback registered");
    
    return ESP_OK;
}

esp_err_t wifi_manager_unregister_event_callback(wifi_manager_event_callback_t callback) {
    WIFI_MANAGER_CHECK_INITIALIZED();
    
    if (callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    WIFI_MANAGER_LOCK();
    
    wifi_manager_callback_entry_t* current = g_wifi_manager.callback_list;
    wifi_manager_callback_entry_t* prev = NULL;
    
    while (current != NULL) {
        if (current->callback == callback) {
            if (prev == NULL) {
                g_wifi_manager.callback_list = current->next;
            } else {
                prev->next = current->next;
            }
            
            free(current);
            WIFI_MANAGER_UNLOCK();
            ESP_LOGI(TAG, "Event callback unregistered");
            return ESP_OK;
        }
        
        prev = current;
        current = current->next;
    }
    
    WIFI_MANAGER_UNLOCK();
    
    return ESP_ERR_NOT_FOUND;
}