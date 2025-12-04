# WiFi Manager Library

[![Version](https://img.shields.io/badge/version-1.0.0-blue.svg)](https://github.com/esp32/wifi-manager)
[![Framework](https://img.shields.io/badge/framework-ESP--IDF-green.svg)](https://docs.espressif.com/projects/esp-idf/)
[![Platform](https://img.shields.io/badge/platform-ESP32-lightgrey.svg)](https://www.espressif.com/en/products/socs/esp32)
[![License](https://img.shields.io/badge/license-MIT-yellow.svg)](LICENSE)

Comprehensive WiFi management library for ESP32 projects with dual-mode operation, automatic fallback mechanisms, network scanning, and configuration management integration.

## Features

### Core WiFi Management
- **Dual Mode Operation**: Simultaneous Access Point (AP) and Station (STA) modes
- **Smart Mode Switching**: Automatic APSTA → STA switching when connection is stable
- **Automatic Fallback**: Falls back to AP mode when STA connection fails
- **Connection Retry Logic**: Configurable retry attempts with exponential backoff

### Network Scanning
- **Synchronous & Asynchronous Scanning**: Blocking and non-blocking scan operations
- **Custom Scan Configuration**: Target channels, hidden networks, passive scanning
- **Scan Result Caching**: Store and retrieve previous scan results
- **Real-time Progress Monitoring**: Track scan progress and estimated time remaining

### Configuration Management
- **Multiple Storage Options**: JSON files + NVS backup
- **Configuration Validation**: Input validation for all settings
- **Hot Configuration Updates**: Apply configuration changes without restart
- **Automatic Credential Saving**: Optional credential persistence

### Network Configuration
- **Static IP & DHCP**: Full IP configuration support
- **DNS Configuration**: Primary and secondary DNS servers
- **Network Validation**: IP address and configuration validation
- **Dynamic IP Changes**: Apply IP settings without reconnection

### Advanced Features
- **Event-Driven Architecture**: Comprehensive event system with callbacks
- **Thread-Safe Operations**: Mutex protection for all operations
- **Connection Monitoring**: Signal strength and connectivity testing
- **Power Management**: Optional power saving features
- **Status Monitoring**: Comprehensive status reporting and diagnostics

## Quick Start

### 1. Installation

Add to your `platformio.ini`:
```ini
[env:esp32dev]
platform = espressif32
framework = espidf
lib_deps = 
    WiFi Manager
    Config Manager
    AP Manager
```

### 2. Basic Usage

```c
#include "wifi_manager.h"

void app_main(void) {
    // Initialize
    ESP_ERROR_CHECK(wifi_manager_init());
    
    // Start Access Point
    ESP_ERROR_CHECK(wifi_manager_start_ap());
    
    // Connect to WiFi
    ESP_ERROR_CHECK(wifi_manager_connect("YourSSID", "YourPassword"));
    
    // Monitor status
    wifi_manager_status_t status;
    wifi_manager_get_status(&status);
    
    if (status.sta_connected) {
        printf("Connected! IP: %s\n", status.sta_ip);
    }
}
```

### 3. With Configuration

```c
#include "wifi_manager.h"

void app_main(void) {
    // Get default configuration
    wifi_manager_config_t config;
    wifi_manager_get_default_config(&config);
    
    // Customize settings
    strcpy(config.ap_config.ssid, "MyDevice-Config");
    strcpy(config.ap_config.password, "MyPassword123");
    config.auto_fallback = true;
    config.fallback_timeout_ms = 30000;
    
    // Initialize with custom config
    ESP_ERROR_CHECK(wifi_manager_init_with_config(&config));
    
    // Load saved configuration
    wifi_manager_load_config();
    
    // Start AP
    ESP_ERROR_CHECK(wifi_manager_start_ap());
}
```

## API Documentation

### Initialization & Configuration

#### `wifi_manager_init()`
Initialize WiFi Manager with default configuration.

**Returns:** `ESP_OK` on success, error code on failure.

```c
esp_err_t ret = wifi_manager_init();
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "WiFi Manager init failed: %s", esp_err_to_name(ret));
}
```

#### `wifi_manager_init_with_config(config)`
Initialize WiFi Manager with custom configuration.

**Parameters:**
- `config`: Pointer to `wifi_manager_config_t` structure

**Returns:** `ESP_OK` on success, error code on failure.

```c
wifi_manager_config_t config;
wifi_manager_get_default_config(&config);
// Modify config...
ESP_ERROR_CHECK(wifi_manager_init_with_config(&config));
```

#### `wifi_manager_get_default_config(config)`
Get default configuration structure.

**Parameters:**
- `config`: Pointer to configuration structure to fill

```c
wifi_manager_config_t config;
wifi_manager_get_default_config(&config);
printf("Default AP SSID: %s\n", config.ap_config.ssid);
```

### Access Point Management

#### `wifi_manager_start_ap()`
Start the Access Point with current configuration.

```c
ESP_ERROR_CHECK(wifi_manager_start_ap());
printf("Access Point started\n");
```

#### `wifi_manager_stop_ap()`
Stop the Access Point.

```c
ESP_ERROR_CHECK(wifi_manager_stop_ap());
printf("Access Point stopped\n");
```

#### `wifi_manager_is_ap_active()`
Check if Access Point is currently active.

**Returns:** `true` if AP is active, `false` otherwise.

```c
if (wifi_manager_is_ap_active()) {
    printf("AP is running\n");
}
```

### Station Management

#### `wifi_manager_connect(ssid, password)`
Connect to a WiFi network.

**Parameters:**
- `ssid`: Network SSID (null-terminated string)
- `password`: Network password (can be NULL for open networks)

```c
esp_err_t ret = wifi_manager_connect("HomeWiFi", "mypassword");
if (ret == ESP_OK) {
    printf("Connection initiated\n");
}
```

#### `wifi_manager_disconnect()`
Disconnect from current WiFi network.

```c
ESP_ERROR_CHECK(wifi_manager_disconnect());
printf("Disconnected from WiFi\n");
```

#### `wifi_manager_is_sta_connected()`
Check if station is connected to WiFi.

**Returns:** `true` if connected with valid IP, `false` otherwise.

```c
if (wifi_manager_is_sta_connected()) {
    printf("WiFi is connected\n");
}
```

### Network Scanning

#### `wifi_manager_scan(results, max_results, actual_count)`
Perform synchronous WiFi scan.

**Parameters:**
- `results`: Array to store scan results
- `max_results`: Maximum number of results to store
- `actual_count`: Pointer to store actual number of results found

```c
wifi_manager_network_info_t networks[20];
uint16_t count;

esp_err_t ret = wifi_manager_scan(networks, 20, &count);
if (ret == ESP_OK) {
    printf("Found %d networks:\n", count);
    for (int i = 0; i < count; i++) {
        printf("  %s (%d dBm)\n", networks[i].ssid, networks[i].rssi);
    }
}
```

#### `wifi_manager_scan_async(callback, user_data)`
Start asynchronous WiFi scan with callback.

**Parameters:**
- `callback`: Function to call when scan completes
- `user_data`: User data passed to callback

```c
void scan_callback(const wifi_manager_network_info_t* results, 
                   uint16_t count, void* user_data) {
    printf("Async scan found %d networks\n", count);
}

ESP_ERROR_CHECK(wifi_manager_scan_async(scan_callback, NULL));
```

#### `wifi_manager_is_scan_in_progress()`
Check if a scan is currently in progress.

```c
if (wifi_manager_is_scan_in_progress()) {
    printf("Scan in progress...\n");
}
```

### Status & Monitoring

#### `wifi_manager_get_status(status)`
Get comprehensive WiFi Manager status.

```c
wifi_manager_status_t status;
ESP_ERROR_CHECK(wifi_manager_get_status(&status));

printf("State: %s\n", wifi_manager_state_to_string(status.state));
printf("Mode: %s\n", wifi_manager_mode_to_string(status.mode));
printf("Connected: %s\n", status.sta_connected ? "Yes" : "No");
if (status.sta_connected) {
    printf("SSID: %s\n", status.current_ssid);
    printf("IP: %s\n", status.sta_ip);
    printf("Signal: %d dBm\n", status.rssi);
}
```

#### Status Structure Fields

```c
typedef struct {
    wifi_manager_state_t state;        // Current state
    wifi_manager_mode_t mode;          // Current mode  
    bool ap_active;                    // AP interface active
    bool sta_connected;                // STA interface connected
    char current_ssid[32];             // Connected SSID
    char sta_ip[16];                   // STA IP address
    char ap_ip[16];                    // AP IP address
    int8_t rssi;                       // Signal strength
    uint8_t connected_clients;         // AP client count
    uint32_t uptime_ms;                // Uptime in milliseconds
    char status_message[256];          // Human-readable status
    bool scan_in_progress;             // Scan operation active
    uint16_t scan_results_count;       // Number of scan results
    uint32_t last_scan_duration_ms;    // Last scan duration
} wifi_manager_status_t;
```

### IP Configuration

#### `wifi_manager_apply_static_ip(ip, netmask, gateway, dns1, dns2)`
Apply static IP configuration.

**Parameters:**
- `ip`: IP address string
- `netmask`: Network mask string  
- `gateway`: Gateway address string
- `dns1`: Primary DNS server (optional)
- `dns2`: Secondary DNS server (optional)

```c
ESP_ERROR_CHECK(wifi_manager_apply_static_ip(
    "192.168.1.100",    // IP
    "255.255.255.0",    // Netmask
    "192.168.1.1",      // Gateway
    "8.8.8.8",          // DNS1
    "8.8.4.4"           // DNS2
));
```

#### `wifi_manager_enable_dhcp()`
Enable DHCP for automatic IP configuration.

```c
ESP_ERROR_CHECK(wifi_manager_enable_dhcp());
printf("DHCP enabled\n");
```

### Event Handling

#### `wifi_manager_register_event_callback(callback, user_data)`
Register event callback function.

```c
void my_wifi_callback(wifi_manager_event_t event, void* data, void* user_data) {
    switch (event) {
        case WIFI_MANAGER_EVENT_STA_CONNECTED:
            printf("WiFi connected!\n");
            break;
        case WIFI_MANAGER_EVENT_STA_DISCONNECTED:
            printf("WiFi disconnected!\n");
            break;
        case WIFI_MANAGER_EVENT_AP_STARTED:
            printf("AP started!\n");
            break;
        // ... handle other events
    }
}

ESP_ERROR_CHECK(wifi_manager_register_event_callback(my_wifi_callback, NULL));
```

#### Event Types

| Event | Description | Data |
|-------|-------------|------|
| `WIFI_MANAGER_EVENT_AP_STARTED` | Access Point started | NULL |
| `WIFI_MANAGER_EVENT_AP_STOPPED` | Access Point stopped | NULL |
| `WIFI_MANAGER_EVENT_AP_CLIENT_CONNECTED` | Client connected to AP | `wifi_event_ap_staconnected_t*` |
| `WIFI_MANAGER_EVENT_AP_CLIENT_DISCONNECTED` | Client disconnected from AP | `wifi_event_ap_stadisconnected_t*` |
| `WIFI_MANAGER_EVENT_STA_CONNECTING` | Station connecting | NULL |
| `WIFI_MANAGER_EVENT_STA_CONNECTED` | Station connected | NULL |
| `WIFI_MANAGER_EVENT_STA_DISCONNECTED` | Station disconnected | `wifi_event_sta_disconnected_t*` |
| `WIFI_MANAGER_EVENT_STA_GOT_IP` | Station got IP address | `ip_event_got_ip_t*` |
| `WIFI_MANAGER_EVENT_SCAN_STARTED` | Network scan started | NULL |
| `WIFI_MANAGER_EVENT_SCAN_COMPLETED` | Network scan completed | `uint16_t*` (count) |
| `WIFI_MANAGER_EVENT_MODE_CHANGED` | WiFi mode changed | `wifi_mode_t*` |
| `WIFI_MANAGER_EVENT_ERROR` | Error occurred | Error details |

### Configuration Storage

#### `wifi_manager_load_config()`
Load configuration from storage (JSON + NVS).

```c
esp_err_t ret = wifi_manager_load_config();
if (ret == ESP_OK) {
    printf("Configuration loaded successfully\n");
} else {
    printf("Using default configuration\n");
}
```

#### `wifi_manager_save_config()`
Save current configuration to storage.

```c
ESP_ERROR_CHECK(wifi_manager_save_config());
printf("Configuration saved\n");
```

#### `wifi_manager_reset_config()`
Reset configuration to defaults.

```c
ESP_ERROR_CHECK(wifi_manager_reset_config());
printf("Configuration reset to defaults\n");
```

## Advanced Configuration

### Custom WiFi Manager Configuration

```c
wifi_manager_config_t config;
wifi_manager_get_default_config(&config);

// Access Point Configuration
strcpy(config.ap_config.ssid, "MyDevice-Setup");
strcpy(config.ap_config.password, "Setup123456");
strcpy(config.ap_config.ip, "192.168.4.1");
config.ap_config.channel = 6;
config.ap_config.max_connections = 8;
config.ap_config.auth_mode = WIFI_AUTH_WPA2_PSK;

// Station Configuration
config.sta_config.threshold_auth_mode = WIFI_AUTH_WPA2_PSK;
config.sta_config.threshold_rssi = -80;
config.sta_config.connect_timeout_ms = 15000;
config.sta_config.max_retry = 5;

// IP Configuration
config.ip_config.type = WIFI_MANAGER_IP_STATIC;
strcpy(config.ip_config.ip, "192.168.1.100");
strcpy(config.ip_config.netmask, "255.255.255.0");
strcpy(config.ip_config.gateway, "192.168.1.1");
strcpy(config.ip_config.dns_primary, "8.8.8.8");

// Behavior Settings
config.auto_connect = true;
config.auto_fallback = true;
config.auto_mode_switch = true;
config.fallback_timeout_ms = 60000;
config.save_credentials = true;
config.enable_power_save = false;

// Scan Configuration
config.scan_config.show_hidden = true;
config.scan_config.scan_timeout_ms = 5000;
config.scan_config.max_results = 30;

ESP_ERROR_CHECK(wifi_manager_init_with_config(&config));
```

### Custom Scan Configuration

```c
wifi_manager_scan_config_t scan_config = {
    .target_ssid = "",              // Empty for all networks
    .target_channel = 0,            // 0 for all channels
    .show_hidden = true,            // Include hidden networks
    .passive_scan = false,          // Use active scanning
    .scan_timeout_ms = 10000,       // 10 second timeout
    .max_results = 50               // Up to 50 results
};

wifi_manager_network_info_t networks[50];
uint16_t count;

esp_err_t ret = wifi_manager_scan_with_config(&scan_config, networks, 50, &count);
```

## Integration Examples

### With Config Manager

```c
#include "wifi_manager.h"
#include "config_manager.h"

void setup_with_config_manager(void) {
    // Initialize Config Manager first
    ESP_ERROR_CHECK(config_manager_init());
    
    // Initialize WiFi Manager
    ESP_ERROR_CHECK(wifi_manager_init());
    
    // Load WiFi configuration from Config Manager
    wifi_manager_load_config();
    
    // Start AP
    ESP_ERROR_CHECK(wifi_manager_start_ap());
    
    // Register callback to save config on successful connection
    wifi_manager_register_event_callback(save_on_connect_callback, NULL);
}

void save_on_connect_callback(wifi_manager_event_t event, void* data, void* user_data) {
    if (event == WIFI_MANAGER_EVENT_STA_GOT_IP) {
        // Save configuration when successfully connected
        wifi_manager_save_config();
        ESP_LOGI(TAG, "WiFi configuration saved");
    }
}
```

### With Web Server

```c
#include "wifi_manager.h"
#include <esp_http_server.h>

httpd_handle_t start_web_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    
    if (httpd_start(&server, &config) == ESP_OK) {
        // Register WiFi management endpoints
        httpd_uri_t wifi_scan_uri = {
            .uri = "/api/wifi/scan",
            .method = HTTP_GET,
            .handler = wifi_scan_handler
        };
        httpd_register_uri_handler(server, &wifi_scan_uri);
        
        httpd_uri_t wifi_connect_uri = {
            .uri = "/api/wifi/connect",
            .method = HTTP_POST,
            .handler = wifi_connect_handler
        };
        httpd_register_uri_handler(server, &wifi_connect_uri);
        
        httpd_uri_t wifi_status_uri = {
            .uri = "/api/wifi/status",
            .method = HTTP_GET,
            .handler = wifi_status_handler
        };
        httpd_register_uri_handler(server, &wifi_status_uri);
    }
    
    return server;
}

esp_err_t wifi_status_handler(httpd_req_t *req) {
    wifi_manager_status_t status;
    ESP_ERROR_CHECK(wifi_manager_get_status(&status));
    
    // Create JSON response
    char response[1024];
    snprintf(response, sizeof(response),
        "{"
        "\"state\":\"%s\","
        "\"connected\":%s,"
        "\"ssid\":\"%s\","
        "\"ip\":\"%s\","
        "\"rssi\":%d,"
        "\"uptime\":%lu"
        "}",
        wifi_manager_state_to_string(status.state),
        status.sta_connected ? "true" : "false",
        status.current_ssid,
        status.sta_ip,
        status.rssi,
        status.uptime_ms
    );
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}
```

## Troubleshooting

### Common Issues

#### WiFi Connection Fails
```c
// Enable debug logging
esp_log_level_set("WIFI_MANAGER", ESP_LOG_DEBUG);

// Check signal strength
wifi_manager_status_t status;
wifi_manager_get_status(&status);
if (status.rssi < -80) {
    ESP_LOGW(TAG, "Weak signal: %d dBm", status.rssi);
}

// Increase connection timeout
wifi_manager_config_t config;
wifi_manager_get_config(&config);
config.sta_config.connect_timeout_ms = 30000; // 30 seconds
config.sta_config.max_retry = 10;
wifi_manager_set_config(&config);
```

#### AP Mode Not Working
```c
// Check AP configuration
wifi_manager_config_t config;
wifi_manager_get_config(&config);

// Validate AP settings
if (!wifi_manager_validate_ssid(config.ap_config.ssid)) {
    ESP_LOGE(TAG, "Invalid AP SSID");
}

if (!wifi_manager_validate_password(config.ap_config.password, config.ap_config.auth_mode)) {
    ESP_LOGE(TAG, "Invalid AP password");
}

// Try different channel
config.ap_config.channel = 11;
wifi_manager_set_config(&config);
```

#### Memory Issues
```c
// Check available heap
ESP_LOGI(TAG, "Free heap: %d bytes", esp_get_free_heap_size());

// Reduce scan buffer size
wifi_manager_config_t config;
wifi_manager_get_config(&config);
config.scan_config.max_results = 10; // Reduce from default 20
wifi_manager_set_config(&config);
```

#### Scan Not Finding Networks
```c
// Enable hidden network detection
wifi_manager_scan_config_t scan_config = {
    .show_hidden = true,
    .passive_scan = false,
    .scan_timeout_ms = 10000,
    .max_results = 30
};

// Check if scan is supported in current mode
if (!wifi_manager_is_ap_active()) {
    // Start AP to enable scanning
    wifi_manager_start_ap();
    vTaskDelay(pdMS_TO_TICKS(2000));
}
```

### Debug Information

#### Enable Detailed Logging
```c
// In your main application
esp_log_level_set("WIFI_MANAGER", ESP_LOG_DEBUG);
esp_log_level_set("wifi", ESP_LOG_DEBUG);
esp_log_level_set("esp_netif_lwip", ESP_LOG_DEBUG);
```

#### Memory Usage Monitoring
```c
void print_memory_usage(void) {
    ESP_LOGI(TAG, "=== Memory Usage ===");
    ESP_LOGI(TAG, "Free heap: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Minimum free heap: %d bytes", esp_get_minimum_free_heap_size());
    ESP_LOGI(TAG, "Largest free block: %d bytes", heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    ESP_LOGI(TAG, "===================");
}

// Call periodically in your application
void monitor_task(void* param) {
    while (1) {
        print_memory_usage();
        vTaskDelay(pdMS_TO_TICKS(60000)); // Every minute
    }
}
```

## Performance Considerations

### Memory Usage
- Base memory usage: ~8KB RAM
- Per scan result: ~100 bytes
- Event callbacks: ~50 bytes per callback
- Configuration storage: ~2KB flash

### Network Performance
- Scan duration: 2-8 seconds (depending on channels)
- Connection time: 3-15 seconds (network dependent)
- AP startup time: ~1 second
- Mode switching: ~2 seconds

### Optimization Tips

#### Reduce Memory Usage
```c
// Limit scan results
config.scan_config.max_results = 10;

// Use targeted scanning
strcpy(config.scan_config.target_ssid, "MyNetwork");

// Disable unnecessary features
config.enable_power_save = true;
```

#### Improve Connection Speed
```c
// Pre-configure known networks
wifi_manager_connect("KnownSSID", "KnownPassword");

// Reduce retry attempts for faster fallback
config.sta_config.max_retry = 3;
config.sta_config.connect_timeout_ms = 10000;

// Use static IP to skip DHCP
wifi_manager_apply_static_ip("192.168.1.100", "255.255.255.0", 
                            "192.168.1.1", "8.8.8.8", NULL);
```

## Migration from Monolithic Code

### From Original WiFi Manager

If you're migrating from the original monolithic `wifi_manager.c`, follow these steps:

#### 1. Replace Includes
```c
// Old
#include "wifi_manager.h"

// New
#include "wifi_manager.h"  // Same include, but now it's the library
```

#### 2. Update Initialization
```c
// Old
start_wifi_ap();

// New  
wifi_manager_init();
wifi_manager_start_ap();
```

#### 3. Update Connection Calls
```c
// Old
wifi_connect(ssid, password);

// New
wifi_manager_connect(ssid, password);
```

#### 4. Update Status Checking
```c
// Old
wifi_status_t status = wifi_get_status();

// New
wifi_manager_status_t status;
wifi_manager_get_status(&status);
```

#### 5. Update Scanning
```c
// Old
wifi_scan();
// Results available in global ap_records[]

// New
wifi_manager_network_info_t networks[20];
uint16_t count;
wifi_manager_scan(networks, 20, &count);
```

### Compatibility Layer

If you need to maintain compatibility with existing code:

```c
// Create compatibility wrapper functions
void start_wifi_ap(void) {
    wifi_manager_init();
    wifi_manager_start_ap();
}

void wifi_connect(const char* ssid, const char* password) {
    wifi_manager_connect(ssid, password);
}

esp_err_t wifi_scan(void) {
    static wifi_manager_network_info_t networks[20];
    uint16_t count;
    return wifi_manager_scan(networks, 20, &count);
}

bool wifi_is_connected(void) {
    return wifi_manager_is_sta_connected();
}
```

## Testing

### Unit Tests

```c
#include "unity.h"
#include "wifi_manager.h"

void test_wifi_manager_initialization(void) {
    esp_err_t ret = wifi_manager_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(wifi_manager_is_initialized());
}

void test_config_validation(void) {
    // Test SSID validation
    TEST_ASSERT_TRUE(wifi_manager_validate_ssid("ValidSSID"));
    TEST_ASSERT_FALSE(wifi_manager_validate_ssid(""));
    TEST_ASSERT_FALSE(wifi_manager_validate_ssid(NULL));
    
    // Test password validation  
    TEST_ASSERT_TRUE(wifi_manager_validate_password("12345678", WIFI_AUTH_WPA2_PSK));
    TEST_ASSERT_FALSE(wifi_manager_validate_password("1234567", WIFI_AUTH_WPA2_PSK)); // Too short
    
    // Test IP validation
    TEST_ASSERT_TRUE(wifi_manager_validate_ip("192.168.1.1"));
    TEST_ASSERT_FALSE(wifi_manager_validate_ip("192.168.1.256")); // Invalid IP
}

void test_ap_operations(void) {
    wifi_manager_init();
    
    esp_err_t ret = wifi_manager_start_ap();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Give AP time to start
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    TEST_ASSERT_TRUE(wifi_manager_is_ap_active());
    
    ret = wifi_manager_stop_ap();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    wifi_manager_deinit();
}
```

### Integration Tests

```c
void test_full_connection_cycle(void) {
    // This test requires a real WiFi network
    wifi_manager_init();
    wifi_manager_start_ap();
    
    // Test connection to known network
    esp_err_t ret = wifi_manager_connect("TestNetwork", "TestPassword");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Wait for connection (with timeout)
    int timeout = 30; // 30 seconds
    while (timeout-- > 0 && !wifi_manager_is_sta_connected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    if (wifi_manager_is_sta_connected()) {
        printf("Connection test passed\n");
        
        // Test status
        wifi_manager_status_t status;
        wifi_manager_get_status(&status);
        TEST_ASSERT_TRUE(status.sta_connected);
        TEST_ASSERT_TRUE(strlen(status.sta_ip) > 0);
        
        // Test disconnection
        wifi_manager_disconnect();
        vTaskDelay(pdMS_TO_TICKS(2000));
        TEST_ASSERT_FALSE(wifi_manager_is_sta_connected());
    } else {
        printf("Connection test skipped (no test network available)\n");
    }
    
    wifi_manager_deinit();
}
```

## Dependencies

### Required Libraries
- **Config Manager** (v1.0.0+): Configuration management and storage
- **AP Manager** (v1.0.0+): Access Point management utilities

### ESP-IDF Components
- `esp_wifi`: WiFi driver and management
- `esp_netif`: Network interface abstraction
- `esp_event`: Event handling system
- `esp_http_server`: HTTP server (if using web interface)
- `nvs_flash`: Non-volatile storage
- `freertos`: Real-time operating system
- `cJSON`: JSON parsing and generation

### Optional Dependencies
- **SPIFFS File Manager** (v1.0.0+): For web interface assets
- **MQTT Client Manager** (v1.0.0+): For IoT connectivity

## Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

### Development Guidelines
- Follow ESP-IDF coding standards
- Add unit tests for new features
- Update documentation for API changes
- Test on real hardware before submitting
- Keep memory usage optimized

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- ESP-IDF team for the excellent framework
- ESP32 community for inspiration and feedback
- Contributors to the original WiFi management code

## Support

- [Documentation](https://github.com/esp32/wifi-manager/wiki)
- [Issue Tracker](https://github.com/esp32/wifi-manager/issues)
- [Discussions](https://github.com/esp32/wifi-manager/discussions)
- [Email Support](mailto:support@esp32-libs.com)

---

**Made with love for the ESP32 community**