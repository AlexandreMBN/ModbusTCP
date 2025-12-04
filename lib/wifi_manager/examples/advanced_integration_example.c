/**
 * @file advanced_integration_example.c
 * @brief Advanced WiFi Manager Integration Example
 * 
 * This example demonstrates advanced usage of the WiFi Manager library
 * with integration to other libraries:
 * - WiFi Manager for connectivity
 * - Config Manager for configuration storage
 * - AP Manager for access point management  
 * - Custom configuration and event handling
 * - Multiple connection strategies
 * 
 * @version 1.0.0
 * @date 2024-11-10
 * @author ESP32 Development Team
 */

#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <esp_http_server.h>

/* Include all our libraries */
#include "wifi_manager.h"
#include "config_manager.h"
#include "ap_manager.h"

static const char *TAG = "WIFI_ADVANCED_EXAMPLE";

/* Application configuration structure */
typedef struct {
    char device_name[32];
    bool auto_reconnect;
    uint32_t connection_timeout;
    uint8_t max_retry_attempts;
    bool enable_web_config;
    uint16_t web_server_port;
    wifi_manager_config_t wifi_config;
} app_config_t;

static app_config_t g_app_config;
static EventGroupHandle_t g_app_event_group;
static httpd_handle_t g_web_server = NULL;

/* Event bits */
#define APP_WIFI_CONNECTED_BIT    BIT0
#define APP_WIFI_FAILED_BIT       BIT1
#define APP_CONFIG_UPDATED_BIT    BIT2
#define APP_WEB_SERVER_STARTED_BIT BIT3

/**
 * @brief Initialize application configuration with defaults
 */
void init_app_config(void) {
    memset(&g_app_config, 0, sizeof(app_config_t));
    
    strcpy(g_app_config.device_name, "ESP32-Advanced-Device");
    g_app_config.auto_reconnect = true;
    g_app_config.connection_timeout = 30000; /* 30 seconds */
    g_app_config.max_retry_attempts = 5;
    g_app_config.enable_web_config = true;
    g_app_config.web_server_port = 80;
    
    /* Get default WiFi Manager configuration */
    wifi_manager_get_default_config(&g_app_config.wifi_config);
    
    /* Customize WiFi configuration */
    strcpy(g_app_config.wifi_config.ap_config.ssid, g_app_config.device_name);
    strcpy(g_app_config.wifi_config.ap_config.password, "ConfigMe123");
    g_app_config.wifi_config.auto_fallback = true;
    g_app_config.wifi_config.fallback_timeout_ms = 60000; /* 1 minute */
    g_app_config.wifi_config.save_credentials = true;
}

/**
 * @brief Load application configuration from storage
 */
esp_err_t load_app_config(void) {
    ESP_LOGI(TAG, "📂 Loading application configuration...");
    
    /* Try to load custom app config using Config Manager */
    char* config_json = NULL;
    esp_err_t ret = config_manager_get_string("app_config", &config_json);
    
    if (ret == ESP_OK && config_json != NULL) {
        ESP_LOGI(TAG, "✅ Found saved application configuration");
        
        /* Parse JSON configuration (simplified for example) */
        /* In a real application, you would use cJSON to parse the configuration */
        
        free(config_json);
    } else {
        ESP_LOGI(TAG, "📝 No saved configuration found, using defaults");
    }
    
    /* Load WiFi Manager configuration */
    return wifi_manager_load_config();
}

/**
 * @brief Save application configuration to storage
 */
esp_err_t save_app_config(void) {
    ESP_LOGI(TAG, "💾 Saving application configuration...");
    
    /* Create JSON configuration string (simplified for example) */
    char config_json[512];
    snprintf(config_json, sizeof(config_json),
        "{"
        "\"device_name\":\"%s\","
        "\"auto_reconnect\":%s,"
        "\"connection_timeout\":%lu,"
        "\"max_retry_attempts\":%d,"
        "\"enable_web_config\":%s,"
        "\"web_server_port\":%d"
        "}",
        g_app_config.device_name,
        g_app_config.auto_reconnect ? "true" : "false",
        g_app_config.connection_timeout,
        g_app_config.max_retry_attempts,
        g_app_config.enable_web_config ? "true" : "false",
        g_app_config.web_server_port
    );
    
    /* Save using Config Manager */
    esp_err_t ret = config_manager_set_string("app_config", config_json);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to save app configuration: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Save WiFi configuration */
    ret = wifi_manager_save_config();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to save WiFi configuration: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

/**
 * @brief WiFi scan completion callback
 */
void scan_complete_callback(const wifi_manager_network_info_t* results, uint16_t count, void* user_data) {
    ESP_LOGI(TAG, "🔍 Asynchronous scan completed: %d networks found", count);
    
    /* Find networks with strong signal */
    for (int i = 0; i < count; i++) {
        if (results[i].rssi > -50) {
            ESP_LOGI(TAG, "  📶 Strong signal: %s (%d dBm)", results[i].ssid, results[i].rssi);
        }
    }
}

/**
 * @brief Advanced WiFi event callback with application logic
 */
void advanced_wifi_event_callback(wifi_manager_event_t event, void* data, void* user_data) {
    switch (event) {
        case WIFI_MANAGER_EVENT_AP_STARTED:
            ESP_LOGI(TAG, "✅ Access Point started");
            ESP_LOGI(TAG, "   📱 Connect to '%s' to configure WiFi", g_app_config.wifi_config.ap_config.ssid);
            
            /* Start web server for configuration if enabled */
            if (g_app_config.enable_web_config) {
                /* In a real application, you would start your web server here */
                ESP_LOGI(TAG, "🌐 Web configuration interface would start here");
                xEventGroupSetBits(g_app_event_group, APP_WEB_SERVER_STARTED_BIT);
            }
            break;
            
        case WIFI_MANAGER_EVENT_STA_CONNECTING:
            ESP_LOGI(TAG, "🔄 Connecting to WiFi network...");
            break;
            
        case WIFI_MANAGER_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "📡 Connected to WiFi network");
            break;
            
        case WIFI_MANAGER_EVENT_STA_GOT_IP: {
            ESP_LOGI(TAG, "✅ Got IP address! WiFi connection successful");
            
            /* Get connection details */
            wifi_manager_status_t status;
            if (wifi_manager_get_status(&status) == ESP_OK) {
                ESP_LOGI(TAG, "   📍 IP: %s", status.sta_ip);
                ESP_LOGI(TAG, "   📶 Signal: %d dBm", status.rssi);
                ESP_LOGI(TAG, "   🏷️ SSID: %s", status.current_ssid);
            }
            
            /* Save successful connection */
            save_app_config();
            
            /* Signal successful connection */
            xEventGroupSetBits(g_app_event_group, APP_WIFI_CONNECTED_BIT);
            
            /* Start application services that require internet */
            ESP_LOGI(TAG, "🚀 Starting internet-dependent services...");
            break;
        }
        
        case WIFI_MANAGER_EVENT_STA_DISCONNECTED:
            ESP_LOGW(TAG, "❌ Disconnected from WiFi network");
            
            /* Implement auto-reconnect if enabled */
            if (g_app_config.auto_reconnect) {
                ESP_LOGI(TAG, "🔄 Auto-reconnect enabled, will retry connection...");
                /* The WiFi Manager will handle fallback automatically */
            }
            
            xEventGroupSetBits(g_app_event_group, APP_WIFI_FAILED_BIT);
            break;
            
        case WIFI_MANAGER_EVENT_SCAN_COMPLETED: {
            uint16_t* scan_count = (uint16_t*)data;
            ESP_LOGI(TAG, "🔍 WiFi scan completed: %d networks found", *scan_count);
            
            /* You could implement smart network selection here */
            break;
        }
        
        case WIFI_MANAGER_EVENT_MODE_CHANGED: {
            wifi_mode_t* mode = (wifi_mode_t*)data;
            ESP_LOGI(TAG, "🔄 WiFi mode changed to: %d", *mode);
            break;
        }
        
        default:
            ESP_LOGI(TAG, "📡 WiFi event: %d", event);
            break;
    }
}

/**
 * @brief Demonstrate advanced scanning with custom configuration
 */
void demonstrate_advanced_scanning(void) {
    ESP_LOGI(TAG, "🔍 Starting advanced WiFi scanning demonstration...");
    
    /* Configure custom scan parameters */
    wifi_manager_scan_config_t scan_config;
    memset(&scan_config, 0, sizeof(scan_config));
    
    scan_config.show_hidden = true;
    scan_config.passive_scan = false;
    scan_config.scan_timeout_ms = 5000; /* 5 second timeout */
    scan_config.max_results = 30;
    
    /* Start asynchronous scan with callback */
    ESP_LOGI(TAG, "🚀 Starting asynchronous scan...");
    esp_err_t ret = wifi_manager_scan_async(scan_complete_callback, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to start async scan: %s", esp_err_to_name(ret));
    }
    
    /* Wait for scan to start */
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    /* Monitor scan progress */
    while (wifi_manager_is_scan_in_progress()) {
        uint32_t time_left = wifi_manager_get_scan_time_left_ms();
        ESP_LOGI(TAG, "⏱️ Scan in progress... %lu ms remaining", time_left);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    ESP_LOGI(TAG, "✅ Scan monitoring completed");
}

/**
 * @brief Demonstrate multiple connection strategies
 */
void demonstrate_connection_strategies(void) {
    ESP_LOGI(TAG, "🎯 Demonstrating multiple connection strategies...");
    
    /* Strategy 1: Try to connect to saved network */
    ESP_LOGI(TAG, "📱 Strategy 1: Reconnecting to saved network");
    esp_err_t ret = wifi_manager_reconnect();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ Reconnection initiated");
        
        /* Wait for connection result */
        EventBits_t bits = xEventGroupWaitBits(g_app_event_group,
                                              APP_WIFI_CONNECTED_BIT | APP_WIFI_FAILED_BIT,
                                              pdTRUE, /* Clear bits after waiting */
                                              pdFALSE,
                                              pdMS_TO_TICKS(g_app_config.connection_timeout));
        
        if (bits & APP_WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "🎉 Strategy 1 successful!");
            return;
        }
    }
    
    /* Strategy 2: Scan and try to connect to known networks */
    ESP_LOGI(TAG, "📡 Strategy 2: Scanning for known networks");
    
    /* List of known networks (in a real app, this might come from config) */
    const char* known_networks[] = {
        "HomeNetwork",
        "OfficeWiFi", 
        "PublicWiFi"
    };
    const char* known_passwords[] = {
        "homepass123",
        "officepass456",
        "" /* Open network */
    };
    
    /* Get scan results */
    wifi_manager_network_info_t* networks = malloc(20 * sizeof(wifi_manager_network_info_t));
    if (networks != NULL) {
        uint16_t network_count = 0;
        ret = wifi_manager_scan(networks, 20, &network_count);
        
        if (ret == ESP_OK) {
            /* Try to find known networks */
            for (int i = 0; i < network_count; i++) {
                for (int j = 0; j < 3; j++) {
                    if (strcmp(networks[i].ssid, known_networks[j]) == 0) {
                        ESP_LOGI(TAG, "🎯 Found known network: %s", networks[i].ssid);
                        
                        ret = wifi_manager_connect(known_networks[j], known_passwords[j]);
                        if (ret == ESP_OK) {
                            /* Wait for connection */
                            EventBits_t bits = xEventGroupWaitBits(g_app_event_group,
                                                                  APP_WIFI_CONNECTED_BIT | APP_WIFI_FAILED_BIT,
                                                                  pdTRUE,
                                                                  pdFALSE,
                                                                  pdMS_TO_TICKS(g_app_config.connection_timeout));
                            
                            if (bits & APP_WIFI_CONNECTED_BIT) {
                                ESP_LOGI(TAG, "🎉 Strategy 2 successful!");
                                free(networks);
                                return;
                            }
                        }
                    }
                }
            }
        }
        free(networks);
    }
    
    /* Strategy 3: Fall back to AP mode for manual configuration */
    ESP_LOGI(TAG, "⚙️ Strategy 3: Enabling AP mode for manual configuration");
    
    wifi_manager_status_t status;
    if (wifi_manager_get_status(&status) == ESP_OK) {
        if (!status.ap_active) {
            ret = wifi_manager_start_ap();
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "✅ AP mode activated for manual configuration");
                ESP_LOGI(TAG, "   📱 Connect to '%s' to configure WiFi", 
                        g_app_config.wifi_config.ap_config.ssid);
            }
        }
    }
    
    ESP_LOGI(TAG, "🏁 Connection strategies demonstration completed");
}

/**
 * @brief Network monitoring task
 */
void network_monitor_task(void* param) {
    ESP_LOGI(TAG, "📊 Starting network monitoring task");
    
    while (1) {
        wifi_manager_status_t status;
        if (wifi_manager_get_status(&status) == ESP_OK) {
            
            if (status.sta_connected) {
                /* Monitor connection quality */
                if (status.rssi < -70) {
                    ESP_LOGW(TAG, "⚠️ Poor WiFi signal strength: %d dBm", status.rssi);
                } else if (status.rssi < -50) {
                    ESP_LOGI(TAG, "📶 Good WiFi signal: %d dBm", status.rssi);
                } else {
                    ESP_LOGI(TAG, "📶 Excellent WiFi signal: %d dBm", status.rssi);
                }
                
                /* Perform connectivity test periodically */
                static int connectivity_check_counter = 0;
                if (++connectivity_check_counter >= 12) { /* Every 5 minutes (12 * 25s = 5min) */
                    ESP_LOGI(TAG, "🌐 Performing connectivity test...");
                    
                    /* In a real application, you might implement ping or HTTP check */
                    ESP_LOGI(TAG, "✅ Connectivity test would be performed here");
                    
                    connectivity_check_counter = 0;
                }
            }
            
            /* Log periodic status */
            ESP_LOGI(TAG, "📡 Status: %s | Uptime: %lu min", 
                    status.status_message, status.uptime_ms / 60000);
        }
        
        vTaskDelay(pdMS_TO_TICKS(25000)); /* Check every 25 seconds */
    }
}

/**
 * @brief Application main function
 */
void app_main(void) {
    ESP_LOGI(TAG, "🚀 Starting WiFi Manager Advanced Integration Example");
    
    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    /* Initialize Config Manager */
    ESP_LOGI(TAG, "🔧 Initializing Config Manager...");
    ret = config_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize Config Manager: %s", esp_err_to_name(ret));
        return;
    }
    
    /* Create event group */
    g_app_event_group = xEventGroupCreate();
    
    /* Initialize application configuration */
    init_app_config();
    load_app_config();
    
    /* Initialize WiFi Manager with custom configuration */
    ESP_LOGI(TAG, "🔧 Initializing WiFi Manager with custom configuration...");
    ret = wifi_manager_init_with_config(&g_app_config.wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize WiFi Manager: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "✅ WiFi Manager initialized successfully");
    
    /* Register advanced event callback */
    ret = wifi_manager_register_event_callback(advanced_wifi_event_callback, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to register event callback: %s", esp_err_to_name(ret));
    }
    
    /* Start Access Point */
    ESP_LOGI(TAG, "🏗️ Starting Access Point...");
    ret = wifi_manager_start_ap();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to start Access Point: %s", esp_err_to_name(ret));
        return;
    }
    
    /* Wait for AP to be fully active */
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    /* Demonstrate advanced scanning */
    demonstrate_advanced_scanning();
    
    /* Demonstrate connection strategies */
    demonstrate_connection_strategies();
    
    /* Start network monitoring task */
    xTaskCreate(network_monitor_task, "net_monitor", 4096, NULL, 5, NULL);
    
    /* Main application loop */
    ESP_LOGI(TAG, "🔄 Entering main application loop");
    
    int loop_counter = 0;
    while (1) {
        loop_counter++;
        
        /* Periodic tasks every 5 minutes */
        if (loop_counter % 20 == 0) { /* 20 * 15s = 5min */
            
            ESP_LOGI(TAG, "🔄 Periodic maintenance cycle %d", loop_counter / 20);
            
            /* Save configuration periodically */
            save_app_config();
            
            /* Demonstrate getting current configuration */
            wifi_manager_config_t current_config;
            if (wifi_manager_get_config(&current_config) == ESP_OK) {
                ESP_LOGI(TAG, "📋 Current WiFi mode: %s", 
                        wifi_manager_mode_to_string(current_config.mode));
            }
            
            /* Print detailed status */
            wifi_manager_status_t status;
            if (wifi_manager_get_status(&status) == ESP_OK) {
                ESP_LOGI(TAG, "=== Detailed System Status ===");
                ESP_LOGI(TAG, "Device: %s", g_app_config.device_name);
                ESP_LOGI(TAG, "WiFi State: %s", wifi_manager_state_to_string(status.state));
                ESP_LOGI(TAG, "WiFi Mode: %s", wifi_manager_mode_to_string(status.mode));
                ESP_LOGI(TAG, "STA Connected: %s", status.sta_connected ? "Yes" : "No");
                ESP_LOGI(TAG, "AP Active: %s", status.ap_active ? "Yes" : "No");
                if (status.sta_connected) {
                    ESP_LOGI(TAG, "Connected to: %s (%s)", status.current_ssid, status.sta_ip);
                }
                if (status.ap_active) {
                    ESP_LOGI(TAG, "AP Address: %s", status.ap_ip);
                }
                ESP_LOGI(TAG, "System Uptime: %lu minutes", status.uptime_ms / 60000);
                ESP_LOGI(TAG, "==============================");
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(15000)); /* Main loop every 15 seconds */
    }
    
    /* Cleanup (never reached in this example) */
    ESP_LOGI(TAG, "🧹 Cleaning up...");
    wifi_manager_deinit();
    config_manager_deinit();
    
    ESP_LOGI(TAG, "🏁 Advanced example completed");
}