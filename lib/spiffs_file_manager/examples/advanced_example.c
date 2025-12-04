#include <esp_log.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_http_server.h>
#include <esp_spiffs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sys/stat.h>
#include "spiffs_file_manager.h"

static const char *TAG = "ADVANCED_EXAMPLE";

// Estrutura para dados do sistema
typedef struct {
    char device_name[32];
    char ip_address[16];
    char firmware_version[16];
    uint32_t uptime_seconds;
    size_t free_memory;
    size_t spiffs_used;
    size_t spiffs_total;
    char wifi_ssid[32];
    int wifi_rssi;
    bool wifi_connected;
    uint8_t connected_clients;
    float temperature;
    float humidity;
    uint16_t pressure;
} system_data_t;

static system_data_t g_system_data = {0};
static httpd_handle_t g_server = NULL;

// ============================================================================
// COLETORES DE DADOS DO SISTEMA
// ============================================================================

static void update_system_data(void) {
    // Nome do dispositivo
    strcpy(g_system_data.device_name, "ESP32-WebServer");
    
    // Versão do firmware
    strcpy(g_system_data.firmware_version, "v1.2.3");
    
    // IP Address (simulado - em aplicação real, obter do WiFi)
    strcpy(g_system_data.ip_address, "192.168.4.1");
    
    // Uptime
    g_system_data.uptime_seconds = esp_log_timestamp() / 1000;
    
    // Memória livre
    g_system_data.free_memory = esp_get_free_heap_size();
    
    // Informações SPIFFS
    esp_spiffs_info(NULL, &g_system_data.spiffs_total, &g_system_data.spiffs_used);
    
    // WiFi (simulado)
    strcpy(g_system_data.wifi_ssid, "ESP32-AP");
    g_system_data.wifi_rssi = -45;
    g_system_data.wifi_connected = true;
    g_system_data.connected_clients = 2;
    
    // Sensores (simulados)
    g_system_data.temperature = 23.5 + (rand() % 100) / 10.0;
    g_system_data.humidity = 55.0 + (rand() % 200) / 10.0;
    g_system_data.pressure = 1013 + (rand() % 50) - 25;
}

static void format_uptime(uint32_t seconds, char *buffer, size_t buffer_size) {
    uint32_t hours = seconds / 3600;
    uint32_t minutes = (seconds % 3600) / 60;
    uint32_t secs = seconds % 60;
    
    snprintf(buffer, buffer_size, "%02lu:%02lu:%02lu", hours, minutes, secs);
}

static void format_bytes(size_t bytes, char *buffer, size_t buffer_size) {
    if (bytes >= 1024 * 1024) {
        snprintf(buffer, buffer_size, "%.2f MB", bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        snprintf(buffer, buffer_size, "%.2f KB", bytes / 1024.0);
    } else {
        snprintf(buffer, buffer_size, "%u bytes", bytes);
    }
}

// ============================================================================
// PREPROCESSORS CUSTOMIZADOS
// ============================================================================

static esp_err_t html_preprocessor(const char *filepath, char **content, size_t *content_length) {
    if (!content || !*content) return ESP_ERR_INVALID_ARG;
    
    // Aplicar compressão simples: remover espaços extras
    char *original = *content;
    size_t original_len = strlen(original);
    char *compressed = malloc(original_len + 1);
    
    if (!compressed) return ESP_ERR_NO_MEM;
    
    char *write_pos = compressed;
    char *read_pos = original;
    bool in_whitespace = false;
    
    while (*read_pos) {
        if (*read_pos == ' ' || *read_pos == '\t' || *read_pos == '\n' || *read_pos == '\r') {
            if (!in_whitespace) {
                *write_pos++ = ' ';
                in_whitespace = true;
            }
        } else {
            *write_pos++ = *read_pos;
            in_whitespace = false;
        }
        read_pos++;
    }
    
    *write_pos = '\0';
    
    size_t compressed_len = write_pos - compressed;
    if (compressed_len < original_len) {
        free(original);
        *content = compressed;
        *content_length = compressed_len;
        ESP_LOGD(TAG, "HTML compressed: %d -> %d bytes (%.1f%% reduction)", 
                 original_len, compressed_len, 
                 (float)(original_len - compressed_len) / original_len * 100.0);
    } else {
        free(compressed);
    }
    
    return ESP_OK;
}

static esp_err_t css_preprocessor(const char *filepath, char **content, size_t *content_length) {
    // Preprocessor para CSS: remover comentários e espaços desnecessários
    if (!content || !*content) return ESP_ERR_INVALID_ARG;
    
    char *original = *content;
    size_t original_len = strlen(original);
    char *processed = malloc(original_len + 1);
    
    if (!processed) return ESP_ERR_NO_MEM;
    
    char *write_pos = processed;
    char *read_pos = original;
    bool in_comment = false;
    
    while (*read_pos && *(read_pos + 1)) {
        if (!in_comment && *read_pos == '/' && *(read_pos + 1) == '*') {
            in_comment = true;
            read_pos += 2;
            continue;
        }
        
        if (in_comment && *read_pos == '*' && *(read_pos + 1) == '/') {
            in_comment = false;
            read_pos += 2;
            continue;
        }
        
        if (!in_comment) {
            *write_pos++ = *read_pos;
        }
        
        read_pos++;
    }
    
    if (*read_pos) *write_pos++ = *read_pos;
    *write_pos = '\0';
    
    free(original);
    *content = processed;
    *content_length = write_pos - processed;
    
    ESP_LOGD(TAG, "CSS processed: %d -> %d bytes", original_len, *content_length);
    return ESP_OK;
}

// ============================================================================
// HANDLERS HTTP AVANÇADOS
// ============================================================================

static esp_err_t advanced_status_handler(httpd_req_t *req) {
    ESP_LOGD(TAG, "Advanced status handler called");
    
    // Atualizar dados do sistema
    update_system_data();
    
    // Obter estatísticas do SPIFFS Manager
    uint32_t files_served, cache_hits, cache_misses;
    size_t total_bytes;
    spiffs_manager_get_stats(&files_served, &cache_hits, &cache_misses, &total_bytes);
    
    // Calcular taxa de acerto do cache
    float cache_hit_rate = 0.0;
    if (cache_hits + cache_misses > 0) {
        cache_hit_rate = (float)cache_hits / (cache_hits + cache_misses) * 100.0;
    }
    
    // Preparar buffers para formatação
    char uptime_str[32], free_mem_str[32], spiffs_used_str[32], spiffs_total_str[32];
    char total_bytes_str[32], cache_hit_rate_str[16];
    char wifi_status[32], gpio_status[64], system_logs[512];
    char last_update[64];
    
    format_uptime(g_system_data.uptime_seconds, uptime_str, sizeof(uptime_str));
    format_bytes(g_system_data.free_memory, free_mem_str, sizeof(free_mem_str));
    format_bytes(g_system_data.spiffs_used, spiffs_used_str, sizeof(spiffs_used_str));
    format_bytes(g_system_data.spiffs_total, spiffs_total_str, sizeof(spiffs_total_str));
    format_bytes(total_bytes, total_bytes_str, sizeof(total_bytes_str));
    
    snprintf(cache_hit_rate_str, sizeof(cache_hit_rate_str), "%.1f", cache_hit_rate);
    snprintf(wifi_status, sizeof(wifi_status), "%s", g_system_data.wifi_connected ? "Conectado" : "Desconectado");
    snprintf(gpio_status, sizeof(gpio_status), "GPIO2: HIGH, GPIO4: LOW, GPIO5: INPUT");
    
    // Logs do sistema (simulados)
    snprintf(system_logs, sizeof(system_logs),
        "[%02d:%02d:%02d] Sistema inicializado\\n"
        "[%02d:%02d:%02d] WiFi conectado: %s\\n"
        "[%02d:%02d:%02d] Servidor web iniciado\\n"
        "[%02d:%02d:%02d] SPIFFS montado: %s/%s\\n"
        "[%02d:%02d:%02d] Cache inicializado: %lu hits\\n",
        (int)(g_system_data.uptime_seconds / 3600), 
        (int)((g_system_data.uptime_seconds % 3600) / 60),
        (int)(g_system_data.uptime_seconds % 60),
        (int)(g_system_data.uptime_seconds / 3600), 
        (int)((g_system_data.uptime_seconds % 3600) / 60),
        (int)(g_system_data.uptime_seconds % 60),
        g_system_data.wifi_ssid,
        (int)(g_system_data.uptime_seconds / 3600), 
        (int)((g_system_data.uptime_seconds % 3600) / 60),
        (int)(g_system_data.uptime_seconds % 60),
        (int)(g_system_data.uptime_seconds / 3600), 
        (int)((g_system_data.uptime_seconds % 3600) / 60),
        (int)(g_system_data.uptime_seconds % 60),
        spiffs_used_str, spiffs_total_str,
        (int)(g_system_data.uptime_seconds / 3600), 
        (int)((g_system_data.uptime_seconds % 3600) / 60),
        (int)(g_system_data.uptime_seconds % 60),
        cache_hits
    );
    
    // Timestamp atual
    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now);
    strftime(last_update, sizeof(last_update), "%d/%m/%Y %H:%M:%S", timeinfo);
    
    // Preparar variáveis do template
    spiffs_template_var_t variables[] = {
        {"DEVICE_NAME", g_system_data.device_name},
        {"IP_ADDRESS", g_system_data.ip_address},
        {"UPTIME", uptime_str},
        {"FIRMWARE_VERSION", g_system_data.firmware_version},
        {"FILES_SERVED", NULL}, // Será convertido abaixo
        {"CACHE_HITS", NULL},
        {"CACHE_MISSES", NULL},
        {"TOTAL_BYTES", total_bytes_str},
        {"CACHE_HIT_RATE", cache_hit_rate_str},
        {"FREE_MEMORY", free_mem_str},
        {"SPIFFS_USED", spiffs_used_str},
        {"SPIFFS_TOTAL", spiffs_total_str},
        {"WIFI_STATUS", wifi_status},
        {"WIFI_SSID", g_system_data.wifi_ssid},
        {"WIFI_RSSI", NULL}, // Será convertido abaixo
        {"CONNECTED_CLIENTS", NULL},
        {"TEMPERATURE", NULL},
        {"HUMIDITY", NULL},
        {"PRESSURE", NULL},
        {"GPIO_STATUS", gpio_status},
        {"SYSTEM_LOGS", system_logs},
        {"LAST_UPDATE", last_update}
    };
    
    // Converter números para strings
    char files_served_str[32], cache_hits_str[32], cache_misses_str[32];
    char wifi_rssi_str[16], clients_str[8];
    char temperature_str[16], humidity_str[16], pressure_str[16];
    
    snprintf(files_served_str, sizeof(files_served_str), "%lu", files_served);
    snprintf(cache_hits_str, sizeof(cache_hits_str), "%lu", cache_hits);
    snprintf(cache_misses_str, sizeof(cache_misses_str), "%lu", cache_misses);
    snprintf(wifi_rssi_str, sizeof(wifi_rssi_str), "%d", g_system_data.wifi_rssi);
    snprintf(clients_str, sizeof(clients_str), "%d", g_system_data.connected_clients);
    snprintf(temperature_str, sizeof(temperature_str), "%.1f", g_system_data.temperature);
    snprintf(humidity_str, sizeof(humidity_str), "%.1f", g_system_data.humidity);
    snprintf(pressure_str, sizeof(pressure_str), "%d", g_system_data.pressure);
    
    variables[4].value = files_served_str;
    variables[5].value = cache_hits_str;
    variables[6].value = cache_misses_str;
    variables[14].value = wifi_rssi_str;
    variables[15].value = clients_str;
    variables[16].value = temperature_str;
    variables[17].value = humidity_str;
    variables[18].value = pressure_str;
    
    // Usar handler de template do SPIFFS Manager
    return spiffs_manager_template_handler(req, "examples/status_template.html", variables, 22);
}

static esp_err_t api_advanced_status_handler(httpd_req_t *req) {
    update_system_data();
    
    uint32_t files_served, cache_hits, cache_misses;
    size_t total_bytes;
    spiffs_manager_get_stats(&files_served, &cache_hits, &cache_misses, &total_bytes);
    
    char uptime_str[32];
    format_uptime(g_system_data.uptime_seconds, uptime_str, sizeof(uptime_str));
    
    char json_response[1024];
    snprintf(json_response, sizeof(json_response),
        "{"
        "\"device_name\":\"%s\","
        "\"ip_address\":\"%s\","
        "\"uptime\":\"%s\","
        "\"uptime_seconds\":%lu,"
        "\"firmware_version\":\"%s\","
        "\"files_served\":%lu,"
        "\"cache_hits\":%lu,"
        "\"cache_misses\":%lu,"
        "\"total_bytes\":%u,"
        "\"cache_hit_rate\":%.2f,"
        "\"free_memory\":%u,"
        "\"spiffs_used\":%u,"
        "\"spiffs_total\":%u,"
        "\"wifi\":{"
        "\"connected\":%s,"
        "\"ssid\":\"%s\","
        "\"rssi\":%d,"
        "\"clients\":%d"
        "},"
        "\"sensors\":{"
        "\"temperature\":%.1f,"
        "\"humidity\":%.1f,"
        "\"pressure\":%d"
        "},"
        "\"timestamp\":%lu"
        "}",
        g_system_data.device_name,
        g_system_data.ip_address,
        uptime_str,
        g_system_data.uptime_seconds,
        g_system_data.firmware_version,
        files_served, cache_hits, cache_misses, total_bytes,
        (cache_hits + cache_misses) > 0 ? (float)cache_hits / (cache_hits + cache_misses) * 100.0 : 0.0,
        g_system_data.free_memory,
        g_system_data.spiffs_used,
        g_system_data.spiffs_total,
        g_system_data.wifi_connected ? "true" : "false",
        g_system_data.wifi_ssid,
        g_system_data.wifi_rssi,
        g_system_data.connected_clients,
        g_system_data.temperature,
        g_system_data.humidity,
        g_system_data.pressure,
        (unsigned long)time(NULL)
    );
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    
    return httpd_resp_send(req, json_response, strlen(json_response));
}

// ============================================================================
// TAREFA DE MONITORAMENTO
// ============================================================================

static void monitoring_task(void *pvParameters) {
    while (1) {
        update_system_data();
        
        // Log periódico de estatísticas
        uint32_t files_served, cache_hits, cache_misses;
        size_t total_bytes;
        
        if (spiffs_manager_get_stats(&files_served, &cache_hits, &cache_misses, &total_bytes) == ESP_OK) {
            ESP_LOGI(TAG, "Stats: Files=%lu, Cache=%.1f%%, Memory=%dKB", 
                     files_served,
                     (cache_hits + cache_misses) > 0 ? (float)cache_hits / (cache_hits + cache_misses) * 100.0 : 0.0,
                     g_system_data.free_memory / 1024);
        }
        
        vTaskDelay(pdMS_TO_TICKS(60000)); // A cada minuto
    }
}

// ============================================================================
// FUNÇÃO PRINCIPAL
// ============================================================================

void advanced_spiffs_example(void) {
    ESP_LOGI(TAG, "Iniciando exemplo avançado do SPIFFS File Manager");
    
    // 1. Configuração avançada do SPIFFS Manager
    spiffs_manager_config_t config;
    spiffs_manager_get_default_config(&config);
    
    config.enable_cache = true;
    config.enable_development_headers = false; // Modo produção
    config.max_file_size = 128 * 1024; // 128KB
    config.max_open_files = 15;
    
    esp_err_t ret = spiffs_manager_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar SPIFFS Manager: %s", esp_err_to_name(ret));
        return;
    }
    
    // 2. Registrar MIME types customizados
    spiffs_manager_register_mime_type(".log", "text/plain");
    spiffs_manager_register_mime_type(".config", "application/json");
    spiffs_manager_register_mime_type(".tpl", "text/html");
    
    // 3. Registrar preprocessors
    spiffs_manager_register_preprocessor(".html", html_preprocessor);
    spiffs_manager_register_preprocessor(".css", css_preprocessor);
    
    // 4. Configuração avançada do servidor HTTP
    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.max_uri_handlers = 20;
    server_config.stack_size = 12288; // Stack maior para processamento avançado
    server_config.task_priority = 5;
    server_config.core_id = 1; // Usar core específico
    server_config.max_open_sockets = 8;
    server_config.recv_wait_timeout = 10;
    server_config.send_wait_timeout = 10;
    
    ret = httpd_start(&g_server, &server_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar servidor HTTP: %s", esp_err_to_name(ret));
        spiffs_manager_deinit();
        return;
    }
    
    // 5. Registrar handlers padrão
    ret = spiffs_manager_register_default_handlers(g_server);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao registrar handlers padrão: %s", esp_err_to_name(ret));
        httpd_stop(g_server);
        spiffs_manager_deinit();
        return;
    }
    
    // 6. Registrar handlers avançados
    spiffs_manager_register_custom_handler(g_server, "/status", HTTP_GET, advanced_status_handler);
    spiffs_manager_register_custom_handler(g_server, "/api/status", HTTP_GET, api_advanced_status_handler);
    
    // 7. Inicializar dados do sistema
    update_system_data();
    
    // 8. Criar tarefa de monitoramento
    xTaskCreatePinnedToCore(
        monitoring_task,
        "monitoring",
        4096,
        NULL,
        3,
        NULL,
        0 // Core 0
    );
    
    ESP_LOGI(TAG, "Servidor avançado iniciado com sucesso!");
    ESP_LOGI(TAG, "Funcionalidades disponíveis:");
    ESP_LOGI(TAG, "  - Cache inteligente de arquivos");
    ESP_LOGI(TAG, "  - Preprocessamento HTML/CSS");
    ESP_LOGI(TAG, "  - Templates dinâmicos");
    ESP_LOGI(TAG, "  - API JSON completa");
    ESP_LOGI(TAG, "  - Monitoramento em tempo real");
    ESP_LOGI(TAG, "  - Estatísticas detalhadas");
    ESP_LOGI(TAG, "\\nEndpoints disponíveis:");
    ESP_LOGI(TAG, "  - http://%s/ (interface principal)", g_system_data.ip_address);
    ESP_LOGI(TAG, "  - http://%s/status (status avançado)", g_system_data.ip_address);
    ESP_LOGI(TAG, "  - http://%s/api/status (API JSON)", g_system_data.ip_address);
}

void advanced_spiffs_cleanup(void) {
    if (g_server) {
        ESP_LOGI(TAG, "Parando servidor HTTP avançado");
        httpd_stop(g_server);
        g_server = NULL;
    }
    
    spiffs_manager_deinit();
    ESP_LOGI(TAG, "Exemplo avançado finalizado");
}