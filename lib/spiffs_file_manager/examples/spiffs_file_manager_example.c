#include <esp_log.h>
#include <esp_http_server.h>
#include <esp_spiffs.h>
#include "spiffs_file_manager.h"

static const char *TAG = "SPIFFS_EXAMPLE";

// Exemplo de preprocessor customizado para arquivos .tpl
esp_err_t template_preprocessor(const char *filepath, char **content, size_t *content_length) {
    // Exemplo simples: substitui {{DEVICE_NAME}} por "ESP32-WebServer"
    if (!content || !*content) return ESP_ERR_INVALID_ARG;
    
    char *original = *content;
    char *processed = NULL;
    
    // Procurar por {{DEVICE_NAME}}
    char *pos = strstr(original, "{{DEVICE_NAME}}");
    if (pos) {
        size_t prefix_len = pos - original;
        size_t suffix_len = strlen(pos + 15); // 15 = strlen("{{DEVICE_NAME}}")
        size_t new_len = prefix_len + strlen("ESP32-WebServer") + suffix_len;
        
        processed = malloc(new_len + 1);
        if (processed) {
            strncpy(processed, original, prefix_len);
            strcpy(processed + prefix_len, "ESP32-WebServer");
            strcpy(processed + prefix_len + strlen("ESP32-WebServer"), pos + 15);
            
            free(original);
            *content = processed;
            *content_length = new_len;
        }
    }
    
    return ESP_OK;
}

// Handler customizado para página de status
esp_err_t status_handler(httpd_req_t *req) {
    // Obter estatísticas
    uint32_t files_served, cache_hits, cache_misses;
    size_t total_bytes;
    
    esp_err_t ret = spiffs_manager_get_stats(&files_served, &cache_hits, &cache_misses, &total_bytes);
    if (ret != ESP_OK) {
        return spiffs_manager_send_500(req, "Failed to get statistics");
    }
    
    // Criar template com estatísticas
    spiffs_template_var_t variables[] = {
        {"FILES_SERVED", NULL},
        {"CACHE_HITS", NULL},
        {"CACHE_MISSES", NULL},
        {"TOTAL_BYTES", NULL}
    };
    
    // Converter números para strings
    char files_str[32], hits_str[32], misses_str[32], bytes_str[32];
    snprintf(files_str, sizeof(files_str), "%lu", files_served);
    snprintf(hits_str, sizeof(hits_str), "%lu", cache_hits);
    snprintf(misses_str, sizeof(misses_str), "%lu", cache_misses);
    snprintf(bytes_str, sizeof(bytes_str), "%u", total_bytes);
    
    variables[0].value = files_str;
    variables[1].value = hits_str;
    variables[2].value = misses_str;
    variables[3].value = bytes_str;
    
    // Usar handler de template
    return spiffs_manager_template_handler(req, "html/status.html", variables, 4);
}

// Handler para API JSON
esp_err_t api_status_handler(httpd_req_t *req) {
    uint32_t files_served, cache_hits, cache_misses;
    size_t total_bytes;
    
    esp_err_t ret = spiffs_manager_get_stats(&files_served, &cache_hits, &cache_misses, &total_bytes);
    if (ret != ESP_OK) {
        return spiffs_manager_send_500(req, "Failed to get statistics");
    }
    
    char json_response[256];
    snprintf(json_response, sizeof(json_response),
        "{"
        "\"files_served\":%lu,"
        "\"cache_hits\":%lu,"
        "\"cache_misses\":%lu,"
        "\"total_bytes\":%u,"
        "\"cache_hit_rate\":%.2f"
        "}",
        files_served, cache_hits, cache_misses, total_bytes,
        (cache_hits + cache_misses) > 0 ? (float)cache_hits / (cache_hits + cache_misses) * 100.0 : 0.0
    );
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json_response, strlen(json_response));
}

void spiffs_file_manager_example(void) {
    ESP_LOGI(TAG, "Iniciando exemplo do SPIFFS File Manager");
    
    // 1. Configurar SPIFFS File Manager
    spiffs_manager_config_t config;
    spiffs_manager_get_default_config(&config);
    
    // Customizar configuração
    config.enable_cache = true;
    config.enable_development_headers = true; // Para desenvolvimento
    config.max_file_size = 64 * 1024; // 64KB máximo
    
    // Inicializar
    esp_err_t ret = spiffs_manager_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar SPIFFS File Manager: %s", esp_err_to_name(ret));
        return;
    }
    
    // 2. Registrar MIME types customizados
    spiffs_manager_register_mime_type(".tpl", "text/html");
    spiffs_manager_register_mime_type(".log", "text/plain");
    
    // 3. Registrar preprocessor customizado
    spiffs_manager_register_preprocessor(".tpl", template_preprocessor);
    
    // 4. Criar servidor HTTP
    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.max_uri_handlers = 16;
    server_config.stack_size = 8192;
    
    httpd_handle_t server = NULL;
    ret = httpd_start(&server, &server_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar servidor HTTP: %s", esp_err_to_name(ret));
        spiffs_manager_deinit();
        return;
    }
    
    // 5. Registrar handlers padrão do SPIFFS Manager
    ret = spiffs_manager_register_default_handlers(server);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao registrar handlers padrão: %s", esp_err_to_name(ret));
        httpd_stop(server);
        spiffs_manager_deinit();
        return;
    }
    
    // 6. Registrar handlers customizados
    spiffs_manager_register_custom_handler(server, "/status", HTTP_GET, status_handler);
    spiffs_manager_register_custom_handler(server, "/api/status", HTTP_GET, api_status_handler);
    
    ESP_LOGI(TAG, "Servidor iniciado com sucesso!");
    ESP_LOGI(TAG, "Acesse:");
    ESP_LOGI(TAG, "  - http://192.168.4.1/ (página inicial)");
    ESP_LOGI(TAG, "  - http://192.168.4.1/status (estatísticas)");
    ESP_LOGI(TAG, "  - http://192.168.4.1/api/status (API JSON)");
    
    // 7. Exemplo de uso direto da API
    ESP_LOGI(TAG, "\\nTeste da API de carregamento de arquivos:");
    
    // Testar carregamento de arquivo
    char *content = NULL;
    size_t content_length = 0;
    spiffs_manager_result_t result = spiffs_manager_load_file("html/index.html", &content, &content_length);
    
    if (result == SPIFFS_MANAGER_OK) {
        ESP_LOGI(TAG, "Arquivo carregado: %d bytes", content_length);
        free(content);
    } else {
        ESP_LOGW(TAG, "Falha ao carregar arquivo: %s", spiffs_manager_get_error_string(result));
    }
    
    // Verificar se arquivo existe
    if (spiffs_manager_file_exists("css/styles.css")) {
        ESP_LOGI(TAG, "Arquivo CSS encontrado");
        
        size_t file_size;
        if (spiffs_manager_get_file_size("css/styles.css", &file_size) == ESP_OK) {
            ESP_LOGI(TAG, "Tamanho do CSS: %d bytes", file_size);
        }
    } else {
        ESP_LOGW(TAG, "Arquivo CSS não encontrado");
    }
    
    // 8. Exemplo de processamento de template
    ESP_LOGI(TAG, "\\nTeste de processamento de template:");
    
    const char *template_str = "<h1>Bem-vindo ao {{DEVICE_NAME}}</h1><p>Status: {{STATUS}}</p>";
    spiffs_template_var_t vars[] = {
        {"DEVICE_NAME", "ESP32-WebServer"},
        {"STATUS", "Online"}
    };
    
    char *processed = NULL;
    result = spiffs_manager_process_template(template_str, vars, 2, &processed);
    if (result == SPIFFS_MANAGER_OK) {
        ESP_LOGI(TAG, "Template processado: %s", processed);
        free(processed);
    }
    
    // Servidor continua rodando...
    // Em uma aplicação real, você manteria o servidor rodando
    // ou pararia apenas quando necessário
}

void spiffs_file_manager_cleanup_example(httpd_handle_t server) {
    if (server) {
        ESP_LOGI(TAG, "Parando servidor HTTP");
        httpd_stop(server);
    }
    
    // Obter estatísticas finais
    uint32_t files_served, cache_hits, cache_misses;
    size_t total_bytes;
    
    if (spiffs_manager_get_stats(&files_served, &cache_hits, &cache_misses, &total_bytes) == ESP_OK) {
        ESP_LOGI(TAG, "Estatísticas finais:");
        ESP_LOGI(TAG, "  - Arquivos servidos: %lu", files_served);
        ESP_LOGI(TAG, "  - Cache hits: %lu", cache_hits);
        ESP_LOGI(TAG, "  - Cache misses: %lu", cache_misses);
        ESP_LOGI(TAG, "  - Bytes totais: %u", total_bytes);
        
        if (cache_hits + cache_misses > 0) {
            float hit_rate = (float)cache_hits / (cache_hits + cache_misses) * 100.0;
            ESP_LOGI(TAG, "  - Taxa de acerto do cache: %.2f%%", hit_rate);
        }
    }
    
    // Limpar cache
    spiffs_manager_clear_cache();
    
    // Desinicializar
    spiffs_manager_deinit();
    
    ESP_LOGI(TAG, "SPIFFS File Manager finalizado");
}