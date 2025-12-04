#include "ap_manager.h"
#include "ap_manager_config.h"
#include <string.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <stdio.h>

static const char *TAG = "AP_MANAGER";

// Estado interno da biblioteca
static bool g_initialized = false;
static bool g_wifi_started = false;
static ap_manager_status_t g_status = {0};
static ap_manager_event_cb_t g_event_callback = NULL;

// Objetos WiFi e rede
static esp_netif_t *g_ap_netif = NULL;
static esp_netif_t *g_sta_netif = NULL;

// Synchronization
static SemaphoreHandle_t g_wifi_mutex = NULL;
static SemaphoreHandle_t g_status_mutex = NULL;
static SemaphoreHandle_t g_scan_mutex = NULL;

// Scan results
static wifi_ap_record_t g_scan_records[AP_MANAGER_MAX_APs];
static uint16_t g_scan_count = 0;
static bool g_scan_in_progress = false;

// Event handler instances
static esp_event_handler_instance_t g_wifi_event_handler = NULL;
static esp_event_handler_instance_t g_ip_event_handler = NULL;

// Tasks
static TaskHandle_t g_auto_switch_task = NULL;

// Define event base for custom events
ESP_EVENT_DEFINE_BASE(AP_MANAGER_EVENTS);

// ============================================================================
// Internal Functions
// ============================================================================

static void init_mutexes(void) {
    if (g_wifi_mutex == NULL) {
        g_wifi_mutex = xSemaphoreCreateMutex();
        if (g_wifi_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create WiFi mutex");
        }
    }
    
    if (g_status_mutex == NULL) {
        g_status_mutex = xSemaphoreCreateMutex();
        if (g_status_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create status mutex");
        }
    }
    
    if (g_scan_mutex == NULL) {
        g_scan_mutex = xSemaphoreCreateMutex();
        if (g_scan_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create scan mutex");
        }
    }
}

static void cleanup_mutexes(void) {
    if (g_wifi_mutex) {
        vSemaphoreDelete(g_wifi_mutex);
        g_wifi_mutex = NULL;
    }
    if (g_status_mutex) {
        vSemaphoreDelete(g_status_mutex);
        g_status_mutex = NULL;
    }
    if (g_scan_mutex) {
        vSemaphoreDelete(g_scan_mutex);
        g_scan_mutex = NULL;
    }
}

static esp_err_t safe_wifi_mode_change(wifi_mode_t mode) {
    if (g_wifi_mutex == NULL) {
        init_mutexes();
    }
    
    if (xSemaphoreTake(g_wifi_mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
        esp_err_t ret = esp_wifi_set_mode(mode);
        xSemaphoreGive(g_wifi_mutex);
        return ret;
    }
    ESP_LOGE(TAG, "Timeout waiting for WiFi mutex");
    return ESP_ERR_TIMEOUT;
}

static esp_err_t safe_wifi_start(void) {
    if (g_wifi_mutex == NULL) {
        init_mutexes();
    }
    
    if (xSemaphoreTake(g_wifi_mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
        esp_err_t ret = esp_wifi_start();
        xSemaphoreGive(g_wifi_mutex);
        return ret;
    }
    ESP_LOGE(TAG, "Timeout waiting for WiFi mutex");
    return ESP_ERR_TIMEOUT;
}

static esp_err_t safe_wifi_stop(void) {
    if (g_wifi_mutex == NULL) {
        init_mutexes();
    }
    
    if (xSemaphoreTake(g_wifi_mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
        esp_err_t ret = esp_wifi_stop();
        xSemaphoreGive(g_wifi_mutex);
        return ret;
    }
    ESP_LOGE(TAG, "Timeout waiting for WiFi mutex");
    return ESP_ERR_TIMEOUT;
}

static void update_status_safe(void) {
    if (g_status_mutex == NULL) {
        init_mutexes();
    }
    
    if (xSemaphoreTake(g_status_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        // Update connection status
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            g_status.is_connected = true;
            g_status.rssi = ap_info.rssi;
            strncpy(g_status.current_ssid, (char*)ap_info.ssid, AP_MANAGER_SSID_MAX_LEN - 1);
            g_status.current_ssid[AP_MANAGER_SSID_MAX_LEN - 1] = '\0';
        } else {
            g_status.is_connected = false;
            g_status.current_ssid[0] = '\0';
            g_status.rssi = 0;
        }
        
        // Update IP address if connected
        if (g_status.is_connected && g_sta_netif) {
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(g_sta_netif, &ip_info) == ESP_OK) {
                snprintf(g_status.ip_address, AP_MANAGER_IP_MAX_LEN, IPSTR, IP2STR(&ip_info.ip));
            }
        }
        
        xSemaphoreGive(g_status_mutex);
    }
}

static void call_event_callback(ap_manager_event_id_t event, void *data) {
    if (g_event_callback) {
        g_event_callback(event, data);
    }
    
    // Also post to ESP event loop
    esp_event_post(AP_MANAGER_EVENTS, event, data, 
                   data ? sizeof(ap_manager_status_t) : 0, 
                   portMAX_DELAY);
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data) {
    ESP_LOGI(TAG, "WiFi event: base=%s, id=%ld", event_base, event_id);
    
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "AP started");
                if (g_status_mutex && xSemaphoreTake(g_status_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    g_status.ap_active = true;
                    strncpy(g_status.status_message, "AP started successfully", AP_MANAGER_STATUS_MSG_MAX_LEN - 1);
                    xSemaphoreGive(g_status_mutex);
                }
                call_event_callback(AP_MANAGER_EVENT_AP_STARTED, &g_status);
                break;
                
            case WIFI_EVENT_AP_STOP:
                ESP_LOGI(TAG, "AP stopped");
                if (g_status_mutex && xSemaphoreTake(g_status_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    g_status.ap_active = false;
                    strncpy(g_status.status_message, "AP stopped", AP_MANAGER_STATUS_MSG_MAX_LEN - 1);
                    xSemaphoreGive(g_status_mutex);
                }
                call_event_callback(AP_MANAGER_EVENT_AP_STOPPED, &g_status);
                break;
                
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "STA started");
                break;
                
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "STA connected");
                update_status_safe();
                call_event_callback(AP_MANAGER_EVENT_STA_CONNECTED, &g_status);
                break;
                
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "STA disconnected");
                if (g_status_mutex && xSemaphoreTake(g_status_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    g_status.is_connected = false;
                    g_status.ip_address[0] = '\0';
                    g_status.current_ssid[0] = '\0';
                    g_status.rssi = 0;
                    strncpy(g_status.status_message, "Disconnected from WiFi", AP_MANAGER_STATUS_MSG_MAX_LEN - 1);
                    xSemaphoreGive(g_status_mutex);
                }
                call_event_callback(AP_MANAGER_EVENT_STA_DISCONNECTED, &g_status);
                break;
                
            case WIFI_EVENT_SCAN_DONE:
                ESP_LOGI(TAG, "WiFi scan completed");
                if (g_scan_mutex && xSemaphoreTake(g_scan_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    g_scan_in_progress = false;
                    uint16_t number = AP_MANAGER_MAX_APs;
                    esp_wifi_scan_get_ap_records(&number, g_scan_records);
                    g_scan_count = number;
                    ESP_LOGI(TAG, "Found %d access points", g_scan_count);
                    xSemaphoreGive(g_scan_mutex);
                }
                call_event_callback(AP_MANAGER_EVENT_SCAN_COMPLETED, &g_scan_count);
                break;
                
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "STA got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        
        if (g_status_mutex && xSemaphoreTake(g_status_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            snprintf(g_status.ip_address, AP_MANAGER_IP_MAX_LEN, IPSTR, IP2STR(&event->ip_info.ip));
            snprintf(g_status.status_message, AP_MANAGER_STATUS_MSG_MAX_LEN, 
                    "Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
            xSemaphoreGive(g_status_mutex);
        }
        
        call_event_callback(AP_MANAGER_EVENT_STA_GOT_IP, &g_status);
    }
}

static void auto_switch_task(void *param) {
    uint32_t timeout_ms = *(uint32_t*)param;
    free(param);
    
    ESP_LOGI(TAG, "Auto-switch task started, timeout: %lu ms", timeout_ms);
    
    uint32_t start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    uint32_t current_time;
    
    while (1) {
        current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        // Check if connected
        ap_manager_status_t status = ap_manager_get_status();
        if (status.is_connected && strlen(status.ip_address) > 0) {
            ESP_LOGI(TAG, "STA connected with IP %s, switching to STA-only mode", status.ip_address);
            
            // Switch to STA-only mode
            esp_err_t ret = ap_manager_set_wifi_mode(WIFI_MODE_STA);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Successfully switched to STA-only mode");
            } else {
                ESP_LOGW(TAG, "Failed to switch to STA-only mode: %s", esp_err_to_name(ret));
            }
            break;
        }
        
        // Check timeout
        if ((current_time - start_time) >= timeout_ms) {
            ESP_LOGW(TAG, "Auto-switch timeout reached, keeping APSTA mode");
            break;
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // Check every 1 second
    }
    
    ESP_LOGI(TAG, "Auto-switch task finished");
    g_auto_switch_task = NULL;
    vTaskDelete(NULL);
}

// ============================================================================
// Public API Implementation
// ============================================================================

esp_err_t ap_manager_init(void) {
    if (g_initialized) {
        ESP_LOGW(TAG, "AP Manager already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing AP Manager");
    
    // Initialize mutexes
    init_mutexes();
    
    // Initialize configuration system
    esp_err_t ret = ap_manager_config_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize config system: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize network interfaces
    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize netif: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Create default event loop
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Create network interfaces
    g_ap_netif = esp_netif_create_default_wifi_ap();
    g_sta_netif = esp_netif_create_default_wifi_sta();
    
    if (g_ap_netif == NULL || g_sta_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create network interfaces");
        return ESP_FAIL;
    }
    
    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.static_rx_buf_num = 16;
    cfg.dynamic_rx_buf_num = 32;
    cfg.rx_ba_win = 16;
    
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Register event handlers
    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
                                            &wifi_event_handler, NULL, 
                                            &g_wifi_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WiFi event handler: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, 
                                            &wifi_event_handler, NULL, 
                                            &g_ip_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP event handler: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize status
    memset(&g_status, 0, sizeof(g_status));
    ap_manager_set_status_message("AP Manager initialized");
    
    g_initialized = true;
    ESP_LOGI(TAG, "AP Manager initialized successfully");
    
    return ESP_OK;
}

esp_err_t ap_manager_deinit(void) {
    if (!g_initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Deinitializing AP Manager");
    
    // Stop auto-switch task if running
    if (g_auto_switch_task) {
        vTaskDelete(g_auto_switch_task);
        g_auto_switch_task = NULL;
    }
    
    // Stop WiFi if started
    if (g_wifi_started) {
        safe_wifi_stop();
        g_wifi_started = false;
    }
    
    // Unregister event handlers
    if (g_wifi_event_handler) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, g_wifi_event_handler);
        g_wifi_event_handler = NULL;
    }
    
    if (g_ip_event_handler) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, g_ip_event_handler);
        g_ip_event_handler = NULL;
    }
    
    // Deinitialize WiFi
    esp_wifi_deinit();
    
    // Destroy network interfaces
    if (g_ap_netif) {
        esp_netif_destroy_default_wifi(g_ap_netif);
        g_ap_netif = NULL;
    }
    if (g_sta_netif) {
        esp_netif_destroy_default_wifi(g_sta_netif);
        g_sta_netif = NULL;
    }
    
    // Cleanup mutexes
    cleanup_mutexes();
    
    // Reset state
    g_initialized = false;
    g_event_callback = NULL;
    memset(&g_status, 0, sizeof(g_status));
    
    ESP_LOGI(TAG, "AP Manager deinitialized");
    return ESP_OK;
}

esp_err_t ap_manager_start_ap(void) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "AP Manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting Access Point");
    
    // Load AP configuration
    ap_manager_config_t ap_config;
    esp_err_t ret = ap_manager_config_load_ap(&ap_config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load AP config, using defaults");
        ap_manager_get_default_ap_config(&ap_config);
    }
    
    ESP_LOGI(TAG, "AP Config - SSID: %s, IP: %s, Channel: %d", 
             ap_config.ssid, ap_config.ip, ap_config.channel);
    
    // Configure WiFi AP
    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(ap_config.ssid),
            .max_connection = ap_config.max_connections,
            .channel = ap_config.channel,
            .beacon_interval = 100,
            .authmode = strlen(ap_config.password) > 0 ? WIFI_AUTH_WPA_WPA2_PSK : WIFI_AUTH_OPEN
        }
    };
    
    strncpy((char*)wifi_config.ap.ssid, ap_config.ssid, sizeof(wifi_config.ap.ssid) - 1);
    strncpy((char*)wifi_config.ap.password, ap_config.password, sizeof(wifi_config.ap.password) - 1);
    
    // Set WiFi mode to APSTA to allow both AP and STA
    ret = safe_wifi_mode_change(WIFI_MODE_APSTA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Set AP configuration
    ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP config: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Start WiFi
    ret = safe_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(ret));
        return ret;
    }
    
    g_wifi_started = true;
    
    // Check for saved STA configuration and try to connect
    ap_manager_sta_config_t sta_config;
    if (ap_manager_config_load_sta(&sta_config) == ESP_OK) {
        ESP_LOGI(TAG, "Found saved STA config, attempting connection to: %s", sta_config.ssid);
        
        // Configure and connect to STA
        wifi_config_t sta_wifi_config = {0};
        strncpy((char*)sta_wifi_config.sta.ssid, sta_config.ssid, sizeof(sta_wifi_config.sta.ssid) - 1);
        strncpy((char*)sta_wifi_config.sta.password, sta_config.password, sizeof(sta_wifi_config.sta.password) - 1);
        sta_wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        
        ret = esp_wifi_set_config(WIFI_IF_STA, &sta_wifi_config);
        if (ret == ESP_OK) {
            ret = esp_wifi_connect();
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Attempting to connect to saved WiFi network");
            } else {
                ESP_LOGW(TAG, "Failed to initiate WiFi connection: %s", esp_err_to_name(ret));
            }
        } else {
            ESP_LOGW(TAG, "Failed to set STA config: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGI(TAG, "No saved STA config found");
    }
    
    ESP_LOGI(TAG, "Access Point started successfully");
    return ESP_OK;
}

esp_err_t ap_manager_stop_ap(void) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "AP Manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Stopping Access Point");
    
    if (g_wifi_started) {
        esp_err_t ret = safe_wifi_stop();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to stop WiFi: %s", esp_err_to_name(ret));
        }
        g_wifi_started = false;
    }
    
    // Update status
    if (g_status_mutex && xSemaphoreTake(g_status_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        g_status.ap_active = false;
        g_status.is_connected = false;
        strncpy(g_status.status_message, "WiFi stopped", AP_MANAGER_STATUS_MSG_MAX_LEN - 1);
        xSemaphoreGive(g_status_mutex);
    }
    
    ESP_LOGI(TAG, "Access Point stopped");
    return ESP_OK;
}

esp_err_t ap_manager_set_ap_config(const ap_manager_config_t *config) {
    if (!g_initialized || !config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Setting AP configuration: SSID=%s, IP=%s", config->ssid, config->ip);
    
    esp_err_t ret = ap_manager_config_save_ap(config);
    if (ret == ESP_OK) {
        call_event_callback(AP_MANAGER_EVENT_CONFIG_SAVED, (void*)config);
    }
    
    return ret;
}

esp_err_t ap_manager_get_ap_config(ap_manager_config_t *config) {
    if (!g_initialized || !config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ap_manager_config_load_ap(config);
    if (ret != ESP_OK) {
        ap_manager_get_default_ap_config(config);
        ret = ESP_OK;
    }
    
    return ret;
}

esp_err_t ap_manager_connect_sta(const char *ssid, const char *password) {
    if (!g_initialized || !ssid) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid);
    
    // Save configuration for auto-reconnect
    ap_manager_sta_config_t sta_config;
    strncpy(sta_config.ssid, ssid, AP_MANAGER_SSID_MAX_LEN - 1);
    sta_config.ssid[AP_MANAGER_SSID_MAX_LEN - 1] = '\0';
    
    if (password) {
        strncpy(sta_config.password, password, AP_MANAGER_PASS_MAX_LEN - 1);
        sta_config.password[AP_MANAGER_PASS_MAX_LEN - 1] = '\0';
    } else {
        sta_config.password[0] = '\0';
    }
    
    esp_err_t ret = ap_manager_config_save_sta(&sta_config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save STA config: %s", esp_err_to_name(ret));
    }
    
    // Configure WiFi STA
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password) {
        strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    
    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set STA config: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Connect
    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "WiFi connection initiated");
    return ESP_OK;
}

esp_err_t ap_manager_disconnect_sta(void) {
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Disconnecting from WiFi");
    
    esp_err_t ret = esp_wifi_disconnect();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to disconnect: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

esp_err_t ap_manager_set_sta_config(const ap_manager_sta_config_t *config) {
    if (!g_initialized || !config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return ap_manager_config_save_sta(config);
}

esp_err_t ap_manager_get_sta_config(ap_manager_sta_config_t *config) {
    if (!g_initialized || !config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return ap_manager_config_load_sta(config);
}

esp_err_t ap_manager_start_scan(void) {
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (g_scan_mutex && xSemaphoreTake(g_scan_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (g_scan_in_progress) {
            xSemaphoreGive(g_scan_mutex);
            ESP_LOGW(TAG, "Scan already in progress");
            return ESP_ERR_INVALID_STATE;
        }
        
        g_scan_in_progress = true;
        xSemaphoreGive(g_scan_mutex);
    }
    
    ESP_LOGI(TAG, "Starting WiFi scan");
    
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false
    };
    
    esp_err_t ret = esp_wifi_scan_start(&scan_config, false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start scan: %s", esp_err_to_name(ret));
        if (g_scan_mutex && xSemaphoreTake(g_scan_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            g_scan_in_progress = false;
            xSemaphoreGive(g_scan_mutex);
        }
        return ret;
    }
    
    ESP_LOGI(TAG, "WiFi scan started");
    return ESP_OK;
}

esp_err_t ap_manager_get_scan_results(wifi_ap_record_t *records, uint16_t max_records, uint16_t *count) {
    if (!g_initialized || !records || !count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (g_scan_mutex && xSemaphoreTake(g_scan_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        uint16_t copy_count = (g_scan_count < max_records) ? g_scan_count : max_records;
        memcpy(records, g_scan_records, copy_count * sizeof(wifi_ap_record_t));
        *count = copy_count;
        xSemaphoreGive(g_scan_mutex);
        
        ESP_LOGI(TAG, "Returning %d scan results (total: %d)", copy_count, g_scan_count);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

bool ap_manager_is_scan_in_progress(void) {
    if (!g_initialized) {
        return false;
    }
    
    bool in_progress = false;
    if (g_scan_mutex && xSemaphoreTake(g_scan_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        in_progress = g_scan_in_progress;
        xSemaphoreGive(g_scan_mutex);
    }
    
    return in_progress;
}

ap_manager_status_t ap_manager_get_status(void) {
    ap_manager_status_t status = {0};
    
    if (!g_initialized) {
        return status;
    }
    
    if (g_status_mutex && xSemaphoreTake(g_status_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        memcpy(&status, &g_status, sizeof(ap_manager_status_t));
        xSemaphoreGive(g_status_mutex);
    }
    
    return status;
}

bool ap_manager_is_initialized(void) {
    return g_initialized;
}

void ap_manager_set_status_message(const char *message) {
    if (!g_initialized || !message) {
        return;
    }
    
    if (g_status_mutex && xSemaphoreTake(g_status_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        strncpy(g_status.status_message, message, AP_MANAGER_STATUS_MSG_MAX_LEN - 1);
        g_status.status_message[AP_MANAGER_STATUS_MSG_MAX_LEN - 1] = '\0';
        xSemaphoreGive(g_status_mutex);
    }
    
    ESP_LOGI(TAG, "Status: %s", message);
}

esp_err_t ap_manager_set_event_callback(ap_manager_event_cb_t callback) {
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    g_event_callback = callback;
    ESP_LOGI(TAG, "Event callback registered");
    return ESP_OK;
}

esp_err_t ap_manager_unset_event_callback(void) {
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    g_event_callback = NULL;
    ESP_LOGI(TAG, "Event callback unregistered");
    return ESP_OK;
}

esp_err_t ap_manager_set_static_ip(const char *ip, const char *netmask, const char *gateway, const char *dns) {
    if (!g_initialized || !ip || !netmask || !gateway) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!g_sta_netif) {
        ESP_LOGE(TAG, "STA interface not available");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Setting static IP: %s/%s, GW: %s, DNS: %s", ip, netmask, gateway, dns ? dns : "auto");
    
    // Stop DHCP client
    esp_err_t ret = esp_netif_dhcpc_stop(g_sta_netif);
    if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGW(TAG, "Failed to stop DHCP client: %s", esp_err_to_name(ret));
    }
    
    // Configure static IP
    esp_netif_ip_info_t ip_info;
    esp_netif_str_to_ip4(ip, &ip_info.ip);
    esp_netif_str_to_ip4(netmask, &ip_info.netmask);
    esp_netif_str_to_ip4(gateway, &ip_info.gw);
    
    ret = esp_netif_set_ip_info(g_sta_netif, &ip_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set IP info: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configure DNS if provided
    if (dns) {
        esp_netif_dns_info_t dns_info;
        esp_netif_str_to_ip4(dns, &dns_info.ip.u_addr.ip4);
        dns_info.ip.type = ESP_IPADDR_TYPE_V4;
        
        ret = esp_netif_set_dns_info(g_sta_netif, ESP_NETIF_DNS_MAIN, &dns_info);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set DNS: %s", esp_err_to_name(ret));
        }
    }
    
    ESP_LOGI(TAG, "Static IP configuration applied");
    return ESP_OK;
}

esp_err_t ap_manager_enable_dhcp(void) {
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!g_sta_netif) {
        ESP_LOGE(TAG, "STA interface not available");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Enabling DHCP client");
    
    esp_err_t ret = esp_netif_dhcpc_start(g_sta_netif);
    if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
        ESP_LOGE(TAG, "Failed to start DHCP client: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "DHCP client enabled");
    return ESP_OK;
}

esp_err_t ap_manager_auto_switch_to_sta(uint32_t timeout_ms) {
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (g_auto_switch_task) {
        ESP_LOGW(TAG, "Auto-switch task already running");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting auto-switch to STA task (timeout: %lu ms)", timeout_ms);
    
    uint32_t *timeout_param = malloc(sizeof(uint32_t));
    if (!timeout_param) {
        return ESP_ERR_NO_MEM;
    }
    
    *timeout_param = timeout_ms;
    
    BaseType_t ret = xTaskCreate(auto_switch_task, "ap_auto_switch", 
                                4096, timeout_param, 5, &g_auto_switch_task);
    
    if (ret != pdPASS) {
        free(timeout_param);
        ESP_LOGE(TAG, "Failed to create auto-switch task");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

esp_err_t ap_manager_set_wifi_mode(wifi_mode_t mode) {
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Setting WiFi mode to %d", mode);
    return safe_wifi_mode_change(mode);
}

void ap_manager_get_default_ap_config(ap_manager_config_t *config) {
    if (!config) {
        return;
    }
    
    strncpy(config->ssid, AP_MANAGER_DEFAULT_SSID, AP_MANAGER_SSID_MAX_LEN - 1);
    config->ssid[AP_MANAGER_SSID_MAX_LEN - 1] = '\0';
    
    strncpy(config->password, AP_MANAGER_DEFAULT_PASSWORD, AP_MANAGER_PASS_MAX_LEN - 1);
    config->password[AP_MANAGER_PASS_MAX_LEN - 1] = '\0';
    
    strncpy(config->ip, AP_MANAGER_DEFAULT_IP, AP_MANAGER_IP_MAX_LEN - 1);
    config->ip[AP_MANAGER_IP_MAX_LEN - 1] = '\0';
    
    config->channel = AP_MANAGER_DEFAULT_CHANNEL;
    config->max_connections = AP_MANAGER_DEFAULT_MAX_CONN;
    config->hidden = false;
}