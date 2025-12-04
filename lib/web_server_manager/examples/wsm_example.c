/**
 * @file wsm_example.c
 * @brief Exemplo de uso do Web Server Manager
 * 
 * Este arquivo demonstra como integrar o Web Server Manager com outros
 * componentes do sistema ESP32, incluindo WiFi Manager, MQTT Client Manager
 * e Config Manager.
 */

#include "web_server_manager.h"
#include "wifi_manager.h"
#include "mqtt_client_manager.h"
#include "config_manager.h"
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_spiffs.h>

static const char *TAG = "WSM_EXAMPLE";

// Handlers customizados do exemplo
static esp_err_t example_home_handler(httpd_req_t *req);
static esp_err_t example_api_status_handler(httpd_req_t *req);
static esp_err_t example_config_handler(httpd_req_t *req);
static esp_err_t example_wifi_scan_handler(httpd_req_t *req);

// Middleware customizado
static esp_err_t example_api_middleware(httpd_req_t *req);

/**
 * @brief Exemplo completo de inicialização do Web Server Manager
 */
void wsm_example_complete_setup(void)
{
    ESP_LOGI(TAG, "Starting complete Web Server Manager example");

    // 1. Inicializar NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Inicializar SPIFFS
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 10,
        .format_if_mount_failed = true
    };
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));

    // 3. Inicializar Config Manager
    config_manager_config_t config_cfg = {
        .use_nvs = true,
        .use_spiffs = true,
        .backup_enabled = true
    };
    ESP_ERROR_CHECK(config_manager_init(&config_cfg));

    // 4. Inicializar WiFi Manager
    wifi_manager_config_t wifi_cfg = {
        .max_retry = 5,
        .retry_interval = 5000,
        .ap_timeout = 300,
        .enable_smartconfig = true
    };
    ESP_ERROR_CHECK(wifi_manager_init(&wifi_cfg));

    // 5. Inicializar MQTT Client Manager
    mqtt_client_manager_config_t mqtt_cfg = {
        .enable_ssl = false,
        .keep_alive = 60,
        .auto_reconnect = true,
        .max_reconnect_attempts = 10
    };
    ESP_ERROR_CHECK(mqtt_client_manager_init(&mqtt_cfg));

    // 6. Configurar Web Server Manager
    wsm_config_t wsm_cfg = {
        .server_config = {
            .task_priority = 5,
            .stack_size = 8192,
            .max_uri_handlers = 20,
            .max_open_sockets = 7,
            .backlog_conn = 5,
            .lru_purge_enable = true,
            .recv_wait_timeout = 5,
            .send_wait_timeout = 5
        },
        .auth_config = {
            .session_timeout = 1800,  // 30 minutos
            .max_sessions = 5,
            .require_auth = true,
            .users = {
                {"admin", "admin123", WSM_USER_LEVEL_ADMIN},
                {"user", "user123", WSM_USER_LEVEL_BASIC},
                {NULL, NULL, WSM_USER_LEVEL_NONE}
            }
        },
        .template_config = {
            .cache_enabled = true,
            .max_cached_templates = 10,
            .template_root = "/spiffs/templates"
        },
        .static_config = {
            .cache_enabled = true,
            .max_cached_files = 20,
            .static_root = "/spiffs/static",
            .enable_etag = true,
            .enable_compression = true
        }
    };

    // 7. Inicializar Web Server Manager
    ESP_ERROR_CHECK(wsm_init(&wsm_cfg));

    // 8. Registrar handlers padrão
    ESP_ERROR_CHECK(wsm_register_default_handlers());

    // 9. Registrar handlers customizados
    wsm_route_config_t custom_routes[] = {
        {"/", HTTP_GET, example_home_handler, WSM_USER_LEVEL_NONE, NULL, false},
        {"/api/status", HTTP_GET, example_api_status_handler, WSM_USER_LEVEL_BASIC, example_api_middleware, false},
        {"/config", HTTP_GET, example_config_handler, WSM_USER_LEVEL_ADMIN, NULL, true},
        {"/api/wifi/scan", HTTP_GET, example_wifi_scan_handler, WSM_USER_LEVEL_BASIC, example_api_middleware, false}
    };

    ESP_ERROR_CHECK(wsm_register_routes(custom_routes, 
                                        sizeof(custom_routes) / sizeof(custom_routes[0])));

    // 10. Configurar middleware global
    wsm_middleware_config_t middleware_cfg = {
        .cors_enabled = true,
        .cors_origins = "*",
        .cors_methods = "GET,POST,PUT,DELETE",
        .cors_headers = "Content-Type,Authorization",
        .logging_enabled = true,
        .rate_limit_enabled = true,
        .rate_limit_requests = 100,
        .rate_limit_window = 60,
        .security_headers_enabled = true
    };
    ESP_ERROR_CHECK(wsm_configure_middleware(&middleware_cfg));

    // 11. Iniciar servidor
    ESP_ERROR_CHECK(wsm_start());

    ESP_LOGI(TAG, "Web Server Manager example setup complete");
}

/**
 * @brief Handler da página inicial
 */
static esp_err_t example_home_handler(httpd_req_t *req)
{
    // Verificar se o usuário está logado
    wsm_user_level_t level = wsm_get_user_level(req);
    bool is_authenticated = (level != WSM_USER_LEVEL_NONE);

    // Preparar contexto do template
    wsm_template_context_t context;
    wsm_template_context_init(&context);

    // Informações básicas do sistema
    wsm_template_add_substitution(&context, "DEVICE_NAME", "ESP32 Web Server");
    wsm_template_add_substitution(&context, "ESP_IDF_VERSION", esp_get_idf_version());

    // Status de conectividade
    wifi_manager_status_t wifi_status = wifi_manager_get_status();
    const char *wifi_status_str = (wifi_status == WIFI_MANAGER_CONNECTED) ? "Conectado" : "Desconectado";
    wsm_template_add_substitution(&context, "WIFI_STATUS", wifi_status_str);

    mqtt_client_status_t mqtt_status = mqtt_client_manager_get_status();
    const char *mqtt_status_str = (mqtt_status == MQTT_CLIENT_CONNECTED) ? "Conectado" : "Desconectado";
    wsm_template_add_substitution(&context, "MQTT_STATUS", mqtt_status_str);

    // Informações de autenticação
    wsm_template_add_substitution(&context, "IS_AUTHENTICATED", is_authenticated ? "true" : "false");
    
    const char *user_level_str = "Não logado";
    if (level == WSM_USER_LEVEL_BASIC) user_level_str = "Usuário";
    else if (level == WSM_USER_LEVEL_ADMIN) user_level_str = "Administrador";
    wsm_template_add_substitution(&context, "USER_LEVEL", user_level_str);

    // Estatísticas do servidor
    wsm_stats_t stats;
    wsm_get_stats(&stats);
    
    char requests_str[16];
    snprintf(requests_str, sizeof(requests_str), "%lu", stats.requests_total);
    wsm_template_add_substitution(&context, "TOTAL_REQUESTS", requests_str);

    // Renderizar template (ou HTML inline se não houver template)
    esp_err_t ret = wsm_respond_with_template(req, "home.html", &context);
    if (ret != ESP_OK) {
        // Fallback para HTML inline
        const char *html = 
            "<!DOCTYPE html>"
            "<html lang='pt-BR'>"
            "<head><meta charset='UTF-8'><title>ESP32 Web Server</title>"
            "<style>body{font-family:Arial,sans-serif;margin:40px;background:#f5f5f5;}"
            ".container{max-width:800px;margin:0 auto;background:white;padding:30px;border-radius:8px;box-shadow:0 2px 10px rgba(0,0,0,0.1);}"
            ".status{display:inline-block;padding:4px 8px;border-radius:4px;color:white;font-size:12px;}"
            ".connected{background:#28a745;}.disconnected{background:#dc3545;}"
            ".nav{margin:20px 0;}.nav a{margin-right:15px;text-decoration:none;color:#007bff;}"
            "</style></head>"
            "<body><div class='container'>"
            "<h1>ESP32 Web Server Manager</h1>";

        httpd_resp_set_type(req, "text/html");
        httpd_resp_sendstr_chunk(req, html);

        // Status do sistema
        char status_html[512];
        snprintf(status_html, sizeof(status_html),
                "<h2>Status do Sistema</h2>"
                "<p>WiFi: <span class='status %s'>%s</span></p>"
                "<p>MQTT: <span class='status %s'>%s</span></p>"
                "<p>Usuário: %s</p>"
                "<p>Total de Requisições: %s</p>",
                (wifi_status == WIFI_MANAGER_CONNECTED) ? "connected" : "disconnected", wifi_status_str,
                (mqtt_status == MQTT_CLIENT_CONNECTED) ? "connected" : "disconnected", mqtt_status_str,
                user_level_str, requests_str);
        httpd_resp_sendstr_chunk(req, status_html);

        // Menu de navegação
        if (is_authenticated) {
            httpd_resp_sendstr_chunk(req, 
                "<div class='nav'>"
                "<a href='/system/info'>Informações do Sistema</a>"
                "<a href='/api/status'>API Status</a>");
            
            if (level == WSM_USER_LEVEL_ADMIN) {
                httpd_resp_sendstr_chunk(req, 
                    "<a href='/config'>Configurações</a>"
                    "<a href='/system/restart'>Reiniciar</a>");
            }
            
            httpd_resp_sendstr_chunk(req, "<a href='/logout'>Sair</a>");
        } else {
            httpd_resp_sendstr_chunk(req, "<div class='nav'><a href='/login'>Fazer Login</a>");
        }
        
        httpd_resp_sendstr_chunk(req, "</div></div></body></html>");
        ret = httpd_resp_sendstr_chunk(req, NULL);
    }

    wsm_template_context_cleanup(&context);
    return ret;
}

/**
 * @brief Handler da API de status
 */
static esp_err_t example_api_status_handler(httpd_req_t *req)
{
    // Coletar informações de status
    wifi_manager_status_t wifi_status = wifi_manager_get_status();
    mqtt_client_status_t mqtt_status = mqtt_client_manager_get_status();
    
    wsm_stats_t wsm_stats;
    wsm_get_stats(&wsm_stats);

    // Montar resposta JSON
    char json_response[1024];
    snprintf(json_response, sizeof(json_response),
        "{"
        "\"status\":\"ok\","
        "\"timestamp\":%lld,"
        "\"system\":{"
            "\"uptime\":%llu,"
            "\"free_heap\":%lu,"
            "\"esp_idf_version\":\"%s\""
        "},"
        "\"connectivity\":{"
            "\"wifi\":\"%s\","
            "\"mqtt\":\"%s\""
        "},"
        "\"web_server\":{"
            "\"requests_total\":%lu,"
            "\"requests_success\":%lu,"
            "\"requests_error\":%lu,"
            "\"active_sessions\":%u"
        "}"
        "}",
        esp_timer_get_time() / 1000,
        esp_timer_get_time(),
        esp_get_free_heap_size(),
        esp_get_idf_version(),
        (wifi_status == WIFI_MANAGER_CONNECTED) ? "connected" : "disconnected",
        (mqtt_status == MQTT_CLIENT_CONNECTED) ? "connected" : "disconnected",
        wsm_stats.requests_total,
        wsm_stats.requests_success,
        wsm_stats.requests_error,
        wsm_stats.active_sessions
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, json_response, strlen(json_response));
}

/**
 * @brief Handler da página de configurações
 */
static esp_err_t example_config_handler(httpd_req_t *req)
{
    // Este handler requer nível de administrador
    const char *html = 
        "<!DOCTYPE html>"
        "<html lang='pt-BR'>"
        "<head><meta charset='UTF-8'><title>Configurações</title></head>"
        "<body>"
        "<h1>Configurações do Sistema</h1>"
        "<p>Área restrita para administradores.</p>"
        "<h2>Opções Disponíveis:</h2>"
        "<ul>"
        "<li><a href='/system/info'>Informações do Sistema</a></li>"
        "<li><a href='/system/restart'>Reiniciar Sistema</a></li>"
        "<li><a href='/system/factory-reset'>Factory Reset</a></li>"
        "</ul>"
        "<a href='/'>Voltar à Página Inicial</a>"
        "</body></html>";

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, strlen(html));
}

/**
 * @brief Handler para scan de redes WiFi
 */
static esp_err_t example_wifi_scan_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "WiFi scan requested");

    // Iniciar scan WiFi
    esp_err_t ret = wifi_manager_scan_networks();
    if (ret != ESP_OK) {
        const char *error_json = "{\"error\":\"Failed to start WiFi scan\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, error_json, strlen(error_json));
    }

    // Aguardar scan completar (simplificado para exemplo)
    vTaskDelay(pdMS_TO_TICKS(3000));

    // Obter resultados
    uint16_t network_count = 0;
    wifi_ap_record_t *networks = wifi_manager_get_scan_results(&network_count);

    // Montar resposta JSON
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "{\"networks\":[");

    for (int i = 0; i < network_count && i < 20; i++) {
        char network_json[256];
        snprintf(network_json, sizeof(network_json),
                "%s{\"ssid\":\"%s\",\"rssi\":%d,\"authmode\":%d}",
                (i > 0) ? "," : "",
                (char*)networks[i].ssid,
                networks[i].rssi,
                networks[i].authmode);
        httpd_resp_sendstr_chunk(req, network_json);
    }

    httpd_resp_sendstr_chunk(req, "]}");
    return httpd_resp_sendstr_chunk(req, NULL);
}

/**
 * @brief Middleware customizado para APIs
 */
static esp_err_t example_api_middleware(httpd_req_t *req)
{
    ESP_LOGI(TAG, "API middleware: %s %s", http_method_str(req->method), req->uri);

    // Adicionar headers específicos para APIs
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");

    return ESP_OK;
}

/**
 * @brief Exemplo de setup básico
 */
void wsm_example_basic_setup(void)
{
    ESP_LOGI(TAG, "Starting basic Web Server Manager example");

    // Configuração mínima
    wsm_config_t config = {0};  // Usar valores padrão
    
    // Configurar apenas autenticação básica
    strcpy(config.auth_config.users[0].username, "admin");
    strcpy(config.auth_config.users[0].password, "admin");
    config.auth_config.users[0].level = WSM_USER_LEVEL_ADMIN;

    // Inicializar e iniciar
    ESP_ERROR_CHECK(wsm_init(&config));
    ESP_ERROR_CHECK(wsm_register_default_handlers());
    ESP_ERROR_CHECK(wsm_start());

    ESP_LOGI(TAG, "Basic Web Server Manager example started");
}

/**
 * @brief Exemplo de limpeza e finalização
 */
void wsm_example_cleanup(void)
{
    ESP_LOGI(TAG, "Cleaning up Web Server Manager example");

    // Parar e limpar componentes na ordem inversa
    wsm_stop();
    wsm_deinit();
    
    mqtt_client_manager_deinit();
    wifi_manager_deinit();
    config_manager_deinit();

    ESP_LOGI(TAG, "Web Server Manager example cleanup complete");
}