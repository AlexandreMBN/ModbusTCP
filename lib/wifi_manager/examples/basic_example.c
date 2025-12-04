/**
 * @file basic_example.c
 * @brief Basic WiFi Manager Usage Example
 * 
 * This example demonstrates the basic usage of the WiFi Manager library:
 * - Initialize WiFi Manager with default settings
 * - Start Access Point for configuration
 * - Connect to a known WiFi network
 * - Monitor connection status
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

/* Include WiFi Manager library */
#include "wifi_manager.h"

static const char *TAG = "WIFI_BASIC_EXAMPLE";

/* Known WiFi credentials (replace with your actual credentials) */
#define WIFI_SSID "YourWiFiNetwork"
#define WIFI_PASSWORD "YourPassword"

/* Event bits for coordination */
#define WIFI_CONNECTED_BIT   BIT0
#define WIFI_FAIL_BIT        BIT1

static EventGroupHandle_t s_wifi_event_group;

/**
 * @brief WiFi Manager event callback
 * 
 * @param event Event type
 * @param data Event data
 * @param user_data User data (not used in this example)
 */
void wifi_event_callback(wifi_manager_event_t event, void* data, void* user_data) {
    switch (event) {
        case WIFI_MANAGER_EVENT_AP_STARTED:
            ESP_LOGI(TAG, "✅ Access Point started successfully");
            ESP_LOGI(TAG, "   Connect to the AP to configure WiFi settings");
            break;
            
        case WIFI_MANAGER_EVENT_STA_CONNECTING:
            ESP_LOGI(TAG, "🔄 Connecting to WiFi network...");
            break;
            
        case WIFI_MANAGER_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "📡 Connected to WiFi network");
            break;
            
        case WIFI_MANAGER_EVENT_STA_GOT_IP:
            ESP_LOGI(TAG, "✅ Got IP address! WiFi connection successful");
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            break;
            
        case WIFI_MANAGER_EVENT_STA_DISCONNECTED:
            ESP_LOGW(TAG, "❌ Disconnected from WiFi network");
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            break;
            
        case WIFI_MANAGER_EVENT_SCAN_COMPLETED: {
            uint16_t* scan_count = (uint16_t*)data;
            ESP_LOGI(TAG, "🔍 WiFi scan completed: %d networks found", *scan_count);
            break;
        }
        
        case WIFI_MANAGER_EVENT_MODE_CHANGED:
            ESP_LOGI(TAG, "🔄 WiFi mode changed");
            break;
            
        default:
            ESP_LOGI(TAG, "📡 WiFi event: %d", event);
            break;
    }
}

/**
 * @brief Print current WiFi status
 */
void print_wifi_status(void) {
    wifi_manager_status_t status;
    esp_err_t ret = wifi_manager_get_status(&status);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get WiFi status");
        return;
    }
    
    ESP_LOGI(TAG, "=== WiFi Status ===");
    ESP_LOGI(TAG, "State: %s", wifi_manager_state_to_string(status.state));
    ESP_LOGI(TAG, "Mode: %s", wifi_manager_mode_to_string(status.mode));
    ESP_LOGI(TAG, "AP Active: %s", status.ap_active ? "Yes" : "No");
    ESP_LOGI(TAG, "STA Connected: %s", status.sta_connected ? "Yes" : "No");
    
    if (status.sta_connected) {
        ESP_LOGI(TAG, "Connected SSID: %s", status.current_ssid);
        ESP_LOGI(TAG, "IP Address: %s", status.sta_ip);
        ESP_LOGI(TAG, "Signal Strength: %d dBm", status.rssi);
    }
    
    if (status.ap_active) {
        ESP_LOGI(TAG, "AP IP Address: %s", status.ap_ip);
    }
    
    ESP_LOGI(TAG, "Uptime: %lu seconds", status.uptime_ms / 1000);
    ESP_LOGI(TAG, "Status: %s", status.status_message);
    ESP_LOGI(TAG, "==================");
}

/**
 * @brief Demonstrate WiFi network scanning
 */
void demonstrate_wifi_scan(void) {
    ESP_LOGI(TAG, "🔍 Starting WiFi network scan...");
    
    /* Allocate buffer for scan results */
    wifi_manager_network_info_t* networks = malloc(20 * sizeof(wifi_manager_network_info_t));
    if (networks == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for scan results");
        return;
    }
    
    uint16_t network_count = 0;
    esp_err_t ret = wifi_manager_scan(networks, 20, &network_count);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ Scan completed! Found %d networks:", network_count);
        
        for (int i = 0; i < network_count; i++) {
            ESP_LOGI(TAG, "  [%d] SSID: %-32s | Auth: %-12s | RSSI: %3d dBm | Ch: %2d",
                    i + 1,
                    networks[i].ssid,
                    wifi_manager_auth_mode_to_string(networks[i].auth_mode),
                    networks[i].rssi,
                    networks[i].channel);
        }
    } else {
        ESP_LOGE(TAG, "❌ WiFi scan failed: %s", esp_err_to_name(ret));
    }
    
    free(networks);
}

/**
 * @brief Status monitoring task
 */
void status_monitor_task(void* param) {
    ESP_LOGI(TAG, "📊 Starting status monitor task");
    
    while (1) {
        print_wifi_status();
        vTaskDelay(pdMS_TO_TICKS(30000)); /* Print status every 30 seconds */
    }
}

/**
 * @brief Application main function
 */
void app_main(void) {
    ESP_LOGI(TAG, "🚀 Starting WiFi Manager Basic Example");
    
    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    /* Create event group for coordination */
    s_wifi_event_group = xEventGroupCreate();
    
    /* Initialize WiFi Manager with default configuration */
    ESP_LOGI(TAG, "🔧 Initializing WiFi Manager...");
    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize WiFi Manager: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "✅ WiFi Manager initialized successfully");
    
    /* Register event callback */
    ret = wifi_manager_register_event_callback(wifi_event_callback, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to register event callback: %s", esp_err_to_name(ret));
    }
    
    /* Load configuration from storage */
    ESP_LOGI(TAG, "📂 Loading configuration from storage...");
    wifi_manager_load_config();
    
    /* Start Access Point for configuration */
    ESP_LOGI(TAG, "🏗️ Starting Access Point...");
    ret = wifi_manager_start_ap();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to start Access Point: %s", esp_err_to_name(ret));
        return;
    }
    
    /* Wait a moment for AP to be fully active */
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    /* Demonstrate WiFi scanning */
    demonstrate_wifi_scan();
    
    /* Try to connect to known WiFi network */
    ESP_LOGI(TAG, "🌐 Attempting to connect to known WiFi network: %s", WIFI_SSID);
    ret = wifi_manager_connect(WIFI_SSID, WIFI_PASSWORD);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initiate WiFi connection: %s", esp_err_to_name(ret));
    }
    
    /* Start status monitoring task */
    xTaskCreate(status_monitor_task, "status_monitor", 4096, NULL, 5, NULL);
    
    /* Wait for connection result */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                          WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                          pdFALSE,
                                          pdFALSE,
                                          portMAX_DELAY);
    
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "🎉 Successfully connected to WiFi network!");
        
        /* Print final status */
        print_wifi_status();
        
        /* Demonstrate getting scan results */
        ESP_LOGI(TAG, "📋 Getting previous scan results...");
        wifi_manager_network_info_t* cached_networks = malloc(20 * sizeof(wifi_manager_network_info_t));
        if (cached_networks != NULL) {
            uint16_t cached_count = 0;
            ret = wifi_manager_get_scan_results(cached_networks, 20, &cached_count);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "📱 Retrieved %d cached scan results", cached_count);
            }
            free(cached_networks);
        }
        
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "⚠️ Failed to connect to WiFi network");
        ESP_LOGI(TAG, "📡 Access Point remains active for manual configuration");
    }
    
    /* Main application loop */
    ESP_LOGI(TAG, "🔄 Entering main application loop");
    while (1) {
        /* Check if we're still connected */
        if (wifi_manager_is_sta_connected()) {
            ESP_LOGI(TAG, "✅ WiFi connection is stable");
        } else {
            ESP_LOGI(TAG, "⚠️ WiFi connection lost");
        }
        
        /* Check AP status */
        if (wifi_manager_is_ap_active()) {
            ESP_LOGI(TAG, "📡 Access Point is active for configuration");
        }
        
        vTaskDelay(pdMS_TO_TICKS(60000)); /* Check every minute */
    }
    
    /* Cleanup (this code will never be reached in this example) */
    ESP_LOGI(TAG, "🧹 Cleaning up WiFi Manager");
    wifi_manager_deinit();
    
    ESP_LOGI(TAG, "🏁 Example completed");
}