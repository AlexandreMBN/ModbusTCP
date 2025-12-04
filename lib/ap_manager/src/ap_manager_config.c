#include "ap_manager_config.h"
#include <esp_spiffs.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <cJSON.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

static const char *TAG = "AP_MGR_CONFIG";

// Helper function to ensure directory exists
static esp_err_t ensure_config_dir(void) {
    struct stat st;
    const char *dir_path = "/spiffs/data/config";
    
    if (stat(dir_path, &st) == -1) {
        ESP_LOGI(TAG, "Creating config directory: %s", dir_path);
        if (mkdir(dir_path, 0755) == -1) {
            ESP_LOGE(TAG, "Failed to create config directory");
            return ESP_FAIL;
        }
    }
    
    return ESP_OK;
}

// Helper function to save JSON string to NVS
static esp_err_t save_json_to_nvs(const char *namespace_name, const char *key, const char *json_str) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(namespace_name, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace %s: %s", namespace_name, esp_err_to_name(err));
        return err;
    }
    
    err = nvs_set_str(nvs_handle, key, json_str);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Saved %s to NVS backup", key);
        } else {
            ESP_LOGE(TAG, "Failed to commit %s to NVS: %s", key, esp_err_to_name(err));
        }
    } else {
        ESP_LOGE(TAG, "Failed to set %s in NVS: %s", key, esp_err_to_name(err));
    }
    
    nvs_close(nvs_handle);
    return err;
}

// Helper function to load JSON string from NVS
static esp_err_t load_json_from_nvs(const char *namespace_name, const char *key, char **json_str) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(namespace_name, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    
    size_t required_size = 0;
    err = nvs_get_str(nvs_handle, key, NULL, &required_size);
    if (err == ESP_OK) {
        *json_str = malloc(required_size);
        if (*json_str) {
            err = nvs_get_str(nvs_handle, key, *json_str, &required_size);
            if (err != ESP_OK) {
                free(*json_str);
                *json_str = NULL;
            }
        } else {
            err = ESP_ERR_NO_MEM;
        }
    }
    
    nvs_close(nvs_handle);
    return err;
}

// Helper function to check if file exists
static bool file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0);
}

esp_err_t ap_manager_config_init(void) {
    ESP_LOGI(TAG, "Initializing AP Manager configuration system");
    
    // Initialize SPIFFS if not already done
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize NVS if not already done
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Ensure config directory exists
    ret = ensure_config_dir();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to create config directory");
        // Don't fail here, might still work with NVS fallback
    }
    
    ESP_LOGI(TAG, "AP Manager configuration system initialized");
    return ESP_OK;
}

esp_err_t ap_manager_config_save_ap(const ap_manager_config_t *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Saving AP configuration: SSID=%s, IP=%s", config->ssid, config->ip);
    
    // Create JSON object
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return ESP_ERR_NO_MEM;
    }
    
    cJSON_AddStringToObject(root, "ssid", config->ssid);
    cJSON_AddStringToObject(root, "password", config->password);
    cJSON_AddStringToObject(root, "ip", config->ip);
    cJSON_AddNumberToObject(root, "channel", config->channel);
    cJSON_AddNumberToObject(root, "max_connections", config->max_connections);
    cJSON_AddBoolToObject(root, "hidden", config->hidden);
    
    char *json_string = cJSON_Print(root);
    if (!json_string) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Failed to serialize JSON");
        return ESP_ERR_NO_MEM;
    }
    
    esp_err_t result = ESP_OK;
    
    // Save to SPIFFS first (priority)
    ensure_config_dir();
    FILE *f = fopen(AP_CONFIG_FILE_PATH, "w");
    if (f) {
        size_t written = fwrite(json_string, 1, strlen(json_string), f);
        fclose(f);
        
        if (written == strlen(json_string)) {
            ESP_LOGI(TAG, "AP config saved to SPIFFS: %s", AP_CONFIG_FILE_PATH);
        } else {
            ESP_LOGE(TAG, "Failed to write complete AP config to SPIFFS");
            result = ESP_FAIL;
        }
    } else {
        ESP_LOGW(TAG, "Failed to open AP config file for writing: %s", AP_CONFIG_FILE_PATH);
        result = ESP_FAIL;
    }
    
    // Save to NVS as backup
    esp_err_t nvs_result = save_json_to_nvs(AP_CONFIG_NVS_NAMESPACE, AP_CONFIG_NVS_KEY, json_string);
    if (nvs_result != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save AP config to NVS backup");
        if (result == ESP_OK) {
            result = ESP_FAIL; // Only fail if both SPIFFS and NVS failed
        }
    }
    
    cJSON_Delete(root);
    free(json_string);
    
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "AP configuration saved successfully");
    }
    
    return result;
}

esp_err_t ap_manager_config_load_ap(ap_manager_config_t *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Loading AP configuration");
    
    char *data = NULL;
    cJSON *root = NULL;
    esp_err_t result = ESP_ERR_NOT_FOUND;
    
    // Try to load from SPIFFS first (priority)
    FILE *f = fopen(AP_CONFIG_FILE_PATH, "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        rewind(f);
        
        data = malloc(size + 1);
        if (data) {
            size_t read_size = fread(data, 1, size, f);
            data[size] = '\0';
            fclose(f);
            
            if (read_size == size) {
                root = cJSON_Parse(data);
                if (root) {
                    ESP_LOGI(TAG, "AP config loaded from SPIFFS");
                    result = ESP_OK;
                } else {
                    ESP_LOGE(TAG, "Failed to parse AP config JSON from SPIFFS");
                }
            } else {
                ESP_LOGE(TAG, "Failed to read complete AP config from SPIFFS");
            }
        } else {
            fclose(f);
            ESP_LOGE(TAG, "Failed to allocate memory for AP config");
            return ESP_ERR_NO_MEM;
        }
    } else {
        ESP_LOGI(TAG, "AP config file not found in SPIFFS: %s", AP_CONFIG_FILE_PATH);
    }
    
    // Fallback to NVS if SPIFFS failed
    if (result != ESP_OK) {
        ESP_LOGI(TAG, "Trying to load AP config from NVS backup");
        
        if (data) {
            free(data);
            data = NULL;
        }
        
        esp_err_t nvs_result = load_json_from_nvs(AP_CONFIG_NVS_NAMESPACE, AP_CONFIG_NVS_KEY, &data);
        if (nvs_result == ESP_OK && data) {
            root = cJSON_Parse(data);
            if (root) {
                ESP_LOGI(TAG, "AP config recovered from NVS backup");
                result = ESP_OK;
            } else {
                ESP_LOGE(TAG, "Failed to parse AP config JSON from NVS");
                result = ESP_ERR_INVALID_STATE;
            }
        } else {
            ESP_LOGI(TAG, "AP config not found in NVS backup");
            result = ESP_ERR_NOT_FOUND;
        }
    }
    
    // Parse JSON if successful
    if (result == ESP_OK && root) {
        cJSON *item;
        
        item = cJSON_GetObjectItem(root, "ssid");
        if (cJSON_IsString(item)) {
            strncpy(config->ssid, item->valuestring, AP_MANAGER_SSID_MAX_LEN - 1);
            config->ssid[AP_MANAGER_SSID_MAX_LEN - 1] = '\0';
        } else {
            strncpy(config->ssid, AP_MANAGER_DEFAULT_SSID, AP_MANAGER_SSID_MAX_LEN - 1);
        }
        
        item = cJSON_GetObjectItem(root, "password");
        if (cJSON_IsString(item)) {
            strncpy(config->password, item->valuestring, AP_MANAGER_PASS_MAX_LEN - 1);
            config->password[AP_MANAGER_PASS_MAX_LEN - 1] = '\0';
        } else {
            strncpy(config->password, AP_MANAGER_DEFAULT_PASSWORD, AP_MANAGER_PASS_MAX_LEN - 1);
        }
        
        item = cJSON_GetObjectItem(root, "ip");
        if (cJSON_IsString(item)) {
            strncpy(config->ip, item->valuestring, AP_MANAGER_IP_MAX_LEN - 1);
            config->ip[AP_MANAGER_IP_MAX_LEN - 1] = '\0';
        } else {
            strncpy(config->ip, AP_MANAGER_DEFAULT_IP, AP_MANAGER_IP_MAX_LEN - 1);
        }
        
        item = cJSON_GetObjectItem(root, "channel");
        config->channel = cJSON_IsNumber(item) ? item->valueint : AP_MANAGER_DEFAULT_CHANNEL;
        
        item = cJSON_GetObjectItem(root, "max_connections");
        config->max_connections = cJSON_IsNumber(item) ? item->valueint : AP_MANAGER_DEFAULT_MAX_CONN;
        
        item = cJSON_GetObjectItem(root, "hidden");
        config->hidden = cJSON_IsBool(item) ? cJSON_IsTrue(item) : false;
        
        ESP_LOGI(TAG, "Loaded AP config - SSID: %s, IP: %s, Channel: %d", 
                 config->ssid, config->ip, config->channel);
    }
    
    // Cleanup
    if (root) {
        cJSON_Delete(root);
    }
    if (data) {
        free(data);
    }
    
    return result;
}

esp_err_t ap_manager_config_save_sta(const ap_manager_sta_config_t *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Saving STA configuration: SSID=%s", config->ssid);
    
    // Create JSON object
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return ESP_ERR_NO_MEM;
    }
    
    cJSON_AddStringToObject(root, "ssid", config->ssid);
    cJSON_AddStringToObject(root, "password", config->password);
    
    char *json_string = cJSON_Print(root);
    if (!json_string) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Failed to serialize JSON");
        return ESP_ERR_NO_MEM;
    }
    
    esp_err_t result = ESP_OK;
    
    // Save to SPIFFS first (priority)
    ensure_config_dir();
    FILE *f = fopen(STA_CONFIG_FILE_PATH, "w");
    if (f) {
        size_t written = fwrite(json_string, 1, strlen(json_string), f);
        fclose(f);
        
        if (written == strlen(json_string)) {
            ESP_LOGI(TAG, "STA config saved to SPIFFS: %s", STA_CONFIG_FILE_PATH);
        } else {
            ESP_LOGE(TAG, "Failed to write complete STA config to SPIFFS");
            result = ESP_FAIL;
        }
    } else {
        ESP_LOGW(TAG, "Failed to open STA config file for writing: %s", STA_CONFIG_FILE_PATH);
        result = ESP_FAIL;
    }
    
    // Save to NVS as backup
    esp_err_t nvs_result = save_json_to_nvs(AP_CONFIG_NVS_NAMESPACE, STA_CONFIG_NVS_KEY, json_string);
    if (nvs_result != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save STA config to NVS backup");
        if (result == ESP_OK) {
            result = ESP_FAIL; // Only fail if both SPIFFS and NVS failed
        }
    }
    
    cJSON_Delete(root);
    free(json_string);
    
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "STA configuration saved successfully");
    }
    
    return result;
}

esp_err_t ap_manager_config_load_sta(ap_manager_sta_config_t *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Loading STA configuration");
    
    char *data = NULL;
    cJSON *root = NULL;
    esp_err_t result = ESP_ERR_NOT_FOUND;
    
    // Try to load from SPIFFS first (priority)
    FILE *f = fopen(STA_CONFIG_FILE_PATH, "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        rewind(f);
        
        data = malloc(size + 1);
        if (data) {
            size_t read_size = fread(data, 1, size, f);
            data[size] = '\0';
            fclose(f);
            
            if (read_size == size) {
                root = cJSON_Parse(data);
                if (root) {
                    ESP_LOGI(TAG, "STA config loaded from SPIFFS");
                    result = ESP_OK;
                } else {
                    ESP_LOGE(TAG, "Failed to parse STA config JSON from SPIFFS");
                }
            } else {
                ESP_LOGE(TAG, "Failed to read complete STA config from SPIFFS");
            }
        } else {
            fclose(f);
            ESP_LOGE(TAG, "Failed to allocate memory for STA config");
            return ESP_ERR_NO_MEM;
        }
    } else {
        ESP_LOGI(TAG, "STA config file not found in SPIFFS: %s", STA_CONFIG_FILE_PATH);
    }
    
    // Fallback to NVS if SPIFFS failed
    if (result != ESP_OK) {
        ESP_LOGI(TAG, "Trying to load STA config from NVS backup");
        
        if (data) {
            free(data);
            data = NULL;
        }
        
        esp_err_t nvs_result = load_json_from_nvs(AP_CONFIG_NVS_NAMESPACE, STA_CONFIG_NVS_KEY, &data);
        if (nvs_result == ESP_OK && data) {
            root = cJSON_Parse(data);
            if (root) {
                ESP_LOGI(TAG, "STA config recovered from NVS backup");
                result = ESP_OK;
            } else {
                ESP_LOGE(TAG, "Failed to parse STA config JSON from NVS");
                result = ESP_ERR_INVALID_STATE;
            }
        } else {
            ESP_LOGI(TAG, "STA config not found in NVS backup");
            result = ESP_ERR_NOT_FOUND;
        }
    }
    
    // Parse JSON if successful
    if (result == ESP_OK && root) {
        cJSON *item;
        
        item = cJSON_GetObjectItem(root, "ssid");
        if (cJSON_IsString(item)) {
            strncpy(config->ssid, item->valuestring, AP_MANAGER_SSID_MAX_LEN - 1);
            config->ssid[AP_MANAGER_SSID_MAX_LEN - 1] = '\0';
        } else {
            config->ssid[0] = '\0';
            result = ESP_ERR_INVALID_STATE;
        }
        
        item = cJSON_GetObjectItem(root, "password");
        if (cJSON_IsString(item)) {
            strncpy(config->password, item->valuestring, AP_MANAGER_PASS_MAX_LEN - 1);
            config->password[AP_MANAGER_PASS_MAX_LEN - 1] = '\0';
        } else {
            config->password[0] = '\0';
        }
        
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "Loaded STA config - SSID: %s", config->ssid);
        }
    }
    
    // Cleanup
    if (root) {
        cJSON_Delete(root);
    }
    if (data) {
        free(data);
    }
    
    return result;
}

esp_err_t ap_manager_config_erase_all(void) {
    ESP_LOGI(TAG, "Erasing all AP Manager configurations");
    
    esp_err_t result = ESP_OK;
    
    // Remove SPIFFS files
    if (remove(AP_CONFIG_FILE_PATH) != 0) {
        ESP_LOGW(TAG, "Failed to remove AP config file from SPIFFS");
    } else {
        ESP_LOGI(TAG, "Removed AP config from SPIFFS");
    }
    
    if (remove(STA_CONFIG_FILE_PATH) != 0) {
        ESP_LOGW(TAG, "Failed to remove STA config file from SPIFFS");
    } else {
        ESP_LOGI(TAG, "Removed STA config from SPIFFS");
    }
    
    // Remove NVS entries
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(AP_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_erase_key(nvs_handle, AP_CONFIG_NVS_KEY);
        nvs_erase_key(nvs_handle, STA_CONFIG_NVS_KEY);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "Removed AP Manager configs from NVS");
    } else {
        ESP_LOGW(TAG, "Failed to open NVS for cleanup");
        result = ESP_FAIL;
    }
    
    return result;
}

bool ap_manager_config_ap_exists(void) {
    // Check SPIFFS first
    if (file_exists(AP_CONFIG_FILE_PATH)) {
        return true;
    }
    
    // Check NVS backup
    char *data = NULL;
    esp_err_t result = load_json_from_nvs(AP_CONFIG_NVS_NAMESPACE, AP_CONFIG_NVS_KEY, &data);
    if (result == ESP_OK && data) {
        free(data);
        return true;
    }
    
    return false;
}

bool ap_manager_config_sta_exists(void) {
    // Check SPIFFS first
    if (file_exists(STA_CONFIG_FILE_PATH)) {
        return true;
    }
    
    // Check NVS backup
    char *data = NULL;
    esp_err_t result = load_json_from_nvs(AP_CONFIG_NVS_NAMESPACE, STA_CONFIG_NVS_KEY, &data);
    if (result == ESP_OK && data) {
        free(data);
        return true;
    }
    
    return false;
}

esp_err_t ap_manager_config_export_json(char **json_out) {
    if (!json_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Exporting configurations to JSON");
    
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    
    // Export AP config
    ap_manager_config_t ap_config;
    if (ap_manager_config_load_ap(&ap_config) == ESP_OK) {
        cJSON *ap_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(ap_obj, "ssid", ap_config.ssid);
        cJSON_AddStringToObject(ap_obj, "password", ap_config.password);
        cJSON_AddStringToObject(ap_obj, "ip", ap_config.ip);
        cJSON_AddNumberToObject(ap_obj, "channel", ap_config.channel);
        cJSON_AddNumberToObject(ap_obj, "max_connections", ap_config.max_connections);
        cJSON_AddBoolToObject(ap_obj, "hidden", ap_config.hidden);
        cJSON_AddItemToObject(root, "ap_config", ap_obj);
    }
    
    // Export STA config
    ap_manager_sta_config_t sta_config;
    if (ap_manager_config_load_sta(&sta_config) == ESP_OK) {
        cJSON *sta_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(sta_obj, "ssid", sta_config.ssid);
        cJSON_AddStringToObject(sta_obj, "password", sta_config.password);
        cJSON_AddItemToObject(root, "sta_config", sta_obj);
    }
    
    *json_out = cJSON_Print(root);
    cJSON_Delete(root);
    
    if (!*json_out) {
        ESP_LOGE(TAG, "Failed to serialize export JSON");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "Configurations exported successfully");
    return ESP_OK;
}

esp_err_t ap_manager_config_import_json(const char *json_str) {
    if (!json_str) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Importing configurations from JSON");
    
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse import JSON");
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t result = ESP_OK;
    
    // Import AP config
    cJSON *ap_obj = cJSON_GetObjectItem(root, "ap_config");
    if (ap_obj) {
        ap_manager_config_t ap_config = {0};
        
        cJSON *item = cJSON_GetObjectItem(ap_obj, "ssid");
        if (cJSON_IsString(item)) {
            strncpy(ap_config.ssid, item->valuestring, AP_MANAGER_SSID_MAX_LEN - 1);
        }
        
        item = cJSON_GetObjectItem(ap_obj, "password");
        if (cJSON_IsString(item)) {
            strncpy(ap_config.password, item->valuestring, AP_MANAGER_PASS_MAX_LEN - 1);
        }
        
        item = cJSON_GetObjectItem(ap_obj, "ip");
        if (cJSON_IsString(item)) {
            strncpy(ap_config.ip, item->valuestring, AP_MANAGER_IP_MAX_LEN - 1);
        }
        
        item = cJSON_GetObjectItem(ap_obj, "channel");
        if (cJSON_IsNumber(item)) {
            ap_config.channel = item->valueint;
        }
        
        item = cJSON_GetObjectItem(ap_obj, "max_connections");
        if (cJSON_IsNumber(item)) {
            ap_config.max_connections = item->valueint;
        }
        
        item = cJSON_GetObjectItem(ap_obj, "hidden");
        if (cJSON_IsBool(item)) {
            ap_config.hidden = cJSON_IsTrue(item);
        }
        
        esp_err_t ap_result = ap_manager_config_save_ap(&ap_config);
        if (ap_result != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save imported AP config");
            result = ap_result;
        }
    }
    
    // Import STA config
    cJSON *sta_obj = cJSON_GetObjectItem(root, "sta_config");
    if (sta_obj) {
        ap_manager_sta_config_t sta_config = {0};
        
        cJSON *item = cJSON_GetObjectItem(sta_obj, "ssid");
        if (cJSON_IsString(item)) {
            strncpy(sta_config.ssid, item->valuestring, AP_MANAGER_SSID_MAX_LEN - 1);
        }
        
        item = cJSON_GetObjectItem(sta_obj, "password");
        if (cJSON_IsString(item)) {
            strncpy(sta_config.password, item->valuestring, AP_MANAGER_PASS_MAX_LEN - 1);
        }
        
        esp_err_t sta_result = ap_manager_config_save_sta(&sta_config);
        if (sta_result != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save imported STA config");
            if (result == ESP_OK) result = sta_result;
        }
    }
    
    cJSON_Delete(root);
    
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "Configurations imported successfully");
    }
    
    return result;
}