/**
 * @file web_server_manager.c
 * @brief Web Server Manager - Biblioteca para gerenciamento de servidor web ESP32
 * 
 * Esta biblioteca fornece uma interface modular e robusta para gerenciamento de
 * servidores web em ESP32, incluindo sistema de templates, autenticação,
 * middleware, arquivos estáticos e integração com outras bibliotecas.
 * 
 * @author DPM Team
 * @date 2024
 */

#include "web_server_manager.h"
#include "wsm_middleware.h"
#include "wsm_templates.h"
#include "wsm_static_files.h"

#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

// =============================================================================
// CONSTANTES E DEFINIÇÕES INTERNAS
// =============================================================================

static const char *TAG = "WSM";

#define WSM_MAX_ROUTES              64
#define WSM_MAX_MIDDLEWARES         8
#define WSM_SESSION_COOKIE_NAME     "WSM_SID"
#define WSM_SESSION_TIMEOUT_MS      (30 * 60 * 1000)  // 30 minutos

// =============================================================================
// ESTRUTURAS INTERNAS
// =============================================================================

/**
 * @brief Informações de sessão de usuário
 */
typedef struct {
    char session_id[32];                    /**< ID da sessão */
    wsm_user_level_t user_level;           /**< Nível do usuário */
    uint64_t created_at;                    /**< Timestamp de criação */
    uint64_t last_access;                   /**< Último acesso */
    bool valid;                             /**< Sessão válida */
} wsm_session_t;

/**
 * @brief Rota registrada
 */
typedef struct {
    wsm_route_config_t config;              /**< Configuração da rota */
    httpd_uri_t uri_handler;                /**< Handler ESP-IDF */
    bool active;                            /**< Rota ativa */
} wsm_route_entry_t;

/**
 * @brief Estado interno do Web Server Manager
 */
typedef struct {
    httpd_handle_t server_handle;           /**< Handle do servidor HTTP */
    wsm_config_t config;                    /**< Configuração atual */
    wsm_status_t status;                    /**< Status atual */
    wsm_stats_t stats;                      /**< Estatísticas */
    uint64_t start_time;                    /**< Tempo de inicialização */
    
    // Rotas
    wsm_route_entry_t routes[WSM_MAX_ROUTES];
    size_t route_count;
    
    // Middleware
    wsm_middleware_t middlewares[WSM_MAX_MIDDLEWARES];
    size_t middleware_count;
    
    // Autenticação
    char basic_user[32];
    char basic_pass[64];
    char admin_user[32];
    char admin_pass[64];
    wsm_session_t sessions[8];              /**< Pool de sessões */
    
    // Estado
    bool initialized;
    bool spiffs_mounted;
    
} wsm_context_t;

// =============================================================================
// VARIÁVEIS GLOBAIS INTERNAS
// =============================================================================

static wsm_context_t g_wsm_ctx = {0};

// =============================================================================
// PROTÓTIPOS DE FUNÇÕES INTERNAS
// =============================================================================

static esp_err_t wsm_ensure_spiffs_mounted(void);
static esp_err_t wsm_route_wrapper_handler(httpd_req_t *req);
static esp_err_t wsm_session_create(const char *session_id, wsm_user_level_t level);
static wsm_session_t *wsm_session_find(const char *session_id);
static void wsm_session_cleanup_expired(void);
static esp_err_t wsm_get_session_id_from_cookie(httpd_req_t *req, char *session_id, size_t size);
static esp_err_t wsm_set_session_cookie(httpd_req_t *req, const char *session_id);
static void wsm_generate_session_id(char *session_id, size_t size);
static void wsm_update_stats_request(bool success);

// =============================================================================
// FUNÇÕES DE INICIALIZAÇÃO E CONFIGURAÇÃO
// =============================================================================

esp_err_t wsm_get_default_config(wsm_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(config, 0, sizeof(wsm_config_t));
    
    config->port = 80;
    config->max_open_sockets = 7;
    config->max_uri_handlers = WEB_SERVER_MAX_URI_HANDLERS;
    config->max_resp_headers = 8;
    config->stack_size = 8192;
    config->task_priority = 5;
    config->recv_wait_timeout = 60000;
    config->send_wait_timeout = 60000;
    config->enable_cors = true;
    config->enable_logging = true;
    
    strncpy(config->spiffs_mount_point, "/spiffs", sizeof(config->spiffs_mount_point) - 1);
    strncpy(config->static_files_path, "/spiffs", sizeof(config->static_files_path) - 1);
    strncpy(config->templates_path, "/spiffs/html", sizeof(config->templates_path) - 1);

    return ESP_OK;
}

esp_err_t wsm_init(const wsm_config_t *config)
{
    if (g_wsm_ctx.initialized) {
        ESP_LOGW(TAG, "Web Server Manager already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing Web Server Manager...");

    // Limpar contexto
    memset(&g_wsm_ctx, 0, sizeof(wsm_context_t));

    // Aplicar configuração
    if (config) {
        g_wsm_ctx.config = *config;
    } else {
        wsm_get_default_config(&g_wsm_ctx.config);
    }

    // Inicializar componentes
    g_wsm_ctx.status = WSM_STATUS_STOPPED;
    g_wsm_ctx.start_time = esp_timer_get_time() / 1000000;

    // Credenciais padrão
    strncpy(g_wsm_ctx.basic_user, "adm", sizeof(g_wsm_ctx.basic_user) - 1);
    strncpy(g_wsm_ctx.basic_pass, "adm", sizeof(g_wsm_ctx.basic_pass) - 1);
    strncpy(g_wsm_ctx.admin_user, "root", sizeof(g_wsm_ctx.admin_user) - 1);
    strncpy(g_wsm_ctx.admin_pass, "root", sizeof(g_wsm_ctx.admin_pass) - 1);

    // Montar SPIFFS se necessário
    esp_err_t ret = wsm_ensure_spiffs_mounted();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS mount failed, static files may not work");
    }

    // Inicializar subsistemas
    ret = wsm_template_engine_init(g_wsm_ctx.config.templates_path);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Template engine initialization failed: %s", esp_err_to_name(ret));
    }

    ret = wsm_static_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Static files initialization failed: %s", esp_err_to_name(ret));
    }

    ret = wsm_mime_types_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MIME types initialization failed: %s", esp_err_to_name(ret));
    }

    g_wsm_ctx.initialized = true;

    ESP_LOGI(TAG, "Web Server Manager initialized successfully");
    return ESP_OK;
}

esp_err_t wsm_start(void)
{
    if (!g_wsm_ctx.initialized) {
        ESP_LOGE(TAG, "Web Server Manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (g_wsm_ctx.status == WSM_STATUS_RUNNING) {
        ESP_LOGW(TAG, "Web Server already running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting web server...");
    g_wsm_ctx.status = WSM_STATUS_STARTING;

    // Configurar servidor HTTP
    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    http_config.server_port = g_wsm_ctx.config.port;
    http_config.max_open_sockets = g_wsm_ctx.config.max_open_sockets;
    http_config.max_uri_handlers = g_wsm_ctx.config.max_uri_handlers;
    http_config.max_resp_headers = g_wsm_ctx.config.max_resp_headers;
    http_config.stack_size = g_wsm_ctx.config.stack_size;
    http_config.task_priority = g_wsm_ctx.config.task_priority;
    http_config.recv_wait_timeout = g_wsm_ctx.config.recv_wait_timeout;
    http_config.send_wait_timeout = g_wsm_ctx.config.send_wait_timeout;

    // Iniciar servidor
    esp_err_t ret = httpd_start(&g_wsm_ctx.server_handle, &http_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server: %s", esp_err_to_name(ret));
        g_wsm_ctx.status = WSM_STATUS_ERROR;
        return ret;
    }

    // Registrar handlers de arquivos estáticos padrão
    if (strlen(g_wsm_ctx.config.static_files_path) > 0) {
        wsm_register_default_static_handlers(g_wsm_ctx.config.static_files_path);
    }

    g_wsm_ctx.status = WSM_STATUS_RUNNING;
    
    ESP_LOGI(TAG, "Web server started successfully on port %d", g_wsm_ctx.config.port);
    return ESP_OK;
}

esp_err_t wsm_stop(void)
{
    if (g_wsm_ctx.status != WSM_STATUS_RUNNING) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping web server...");
    g_wsm_ctx.status = WSM_STATUS_STOPPING;

    esp_err_t ret = ESP_OK;
    if (g_wsm_ctx.server_handle) {
        ret = httpd_stop(g_wsm_ctx.server_handle);
        g_wsm_ctx.server_handle = NULL;
    }

    g_wsm_ctx.status = WSM_STATUS_STOPPED;
    
    ESP_LOGI(TAG, "Web server stopped");
    return ret;
}

esp_err_t wsm_deinit(void)
{
    if (!g_wsm_ctx.initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing Web Server Manager...");

    // Parar servidor se ainda estiver rodando
    wsm_stop();

    // Desinicializar subsistemas
    wsm_template_engine_deinit();
    wsm_static_deinit();

    // Limpar contexto
    memset(&g_wsm_ctx, 0, sizeof(wsm_context_t));

    ESP_LOGI(TAG, "Web Server Manager deinitialized");
    return ESP_OK;
}

wsm_status_t wsm_get_status(void)
{
    return g_wsm_ctx.status;
}

esp_err_t wsm_get_stats(wsm_stats_t *stats)
{
    if (!stats) {
        return ESP_ERR_INVALID_ARG;
    }

    *stats = g_wsm_ctx.stats;
    
    // Atualizar uptime
    if (g_wsm_ctx.status == WSM_STATUS_RUNNING) {
        uint64_t current_time = esp_timer_get_time() / 1000000;
        stats->uptime_seconds = (uint32_t)(current_time - g_wsm_ctx.start_time);
    }

    return ESP_OK;
}

// =============================================================================
// FUNÇÕES DE ROTEAMENTO
// =============================================================================

esp_err_t wsm_register_route(const wsm_route_config_t *route_config)
{
    if (!route_config || !route_config->handler) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!g_wsm_ctx.initialized) {
        ESP_LOGE(TAG, "Web Server Manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (g_wsm_ctx.route_count >= WSM_MAX_ROUTES) {
        ESP_LOGE(TAG, "Maximum number of routes reached");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Registering route: %s %s", 
             (route_config->method == HTTP_GET) ? "GET" : 
             (route_config->method == HTTP_POST) ? "POST" : "OTHER",
             route_config->uri);

    // Procurar slot disponível
    size_t slot = g_wsm_ctx.route_count;
    for (size_t i = 0; i < WSM_MAX_ROUTES; i++) {
        if (!g_wsm_ctx.routes[i].active) {
            slot = i;
            break;
        }
    }

    if (slot >= WSM_MAX_ROUTES) {
        ESP_LOGE(TAG, "No available route slots");
        return ESP_ERR_NO_MEM;
    }

    // Configurar entrada de rota
    wsm_route_entry_t *entry = &g_wsm_ctx.routes[slot];
    entry->config = *route_config;
    entry->active = true;

    // Configurar handler ESP-IDF
    entry->uri_handler.uri = entry->config.uri;
    entry->uri_handler.method = route_config->method;
    entry->uri_handler.handler = wsm_route_wrapper_handler;
    entry->uri_handler.user_ctx = entry;

    // Registrar no servidor se estiver rodando
    if (g_wsm_ctx.status == WSM_STATUS_RUNNING && g_wsm_ctx.server_handle) {
        esp_err_t ret = httpd_register_uri_handler(g_wsm_ctx.server_handle, &entry->uri_handler);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register URI handler: %s", esp_err_to_name(ret));
            entry->active = false;
            return ret;
        }
    }

    if (slot == g_wsm_ctx.route_count) {
        g_wsm_ctx.route_count++;
    }

    return ESP_OK;
}

esp_err_t wsm_unregister_route(const char *uri, httpd_method_t method)
{
    if (!uri) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Unregistering route: %s", uri);

    // Procurar rota
    for (size_t i = 0; i < WSM_MAX_ROUTES; i++) {
        wsm_route_entry_t *entry = &g_wsm_ctx.routes[i];
        if (entry->active && 
            strcmp(entry->config.uri, uri) == 0 && 
            entry->config.method == method) {
            
            // Desregistrar do servidor se estiver rodando
            if (g_wsm_ctx.status == WSM_STATUS_RUNNING && g_wsm_ctx.server_handle) {
                httpd_unregister_uri_handler(g_wsm_ctx.server_handle, uri, method);
            }

            // Marcar como inativo
            entry->active = false;
            return ESP_OK;
        }
    }

    ESP_LOGW(TAG, "Route not found: %s", uri);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t wsm_register_routes(const wsm_route_config_t *routes, size_t count)
{
    if (!routes || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;
    size_t registered = 0;

    for (size_t i = 0; i < count; i++) {
        esp_err_t route_ret = wsm_register_route(&routes[i]);
        if (route_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register route %zu: %s", i, esp_err_to_name(route_ret));
            ret = route_ret;
        } else {
            registered++;
        }
    }

    ESP_LOGI(TAG, "Registered %zu out of %zu routes", registered, count);
    return (registered > 0) ? ESP_OK : ret;
}

// =============================================================================
// FUNÇÕES INTERNAS DE ROTEAMENTO
// =============================================================================

static esp_err_t wsm_route_wrapper_handler(httpd_req_t *req)
{
    wsm_route_entry_t *route_entry = (wsm_route_entry_t *)req->user_ctx;
    if (!route_entry || !route_entry->active) {
        wsm_update_stats_request(false);
        return httpd_resp_send_404(req);
    }

    ESP_LOGD(TAG, "Processing request: %s %s", 
             (req->method == HTTP_GET) ? "GET" : 
             (req->method == HTTP_POST) ? "POST" : "OTHER",
             req->uri);

    esp_err_t ret = ESP_OK;

    // Executar middleware se habilitado
    if (route_entry->config.enable_middleware) {
        ret = wsm_execute_middleware_pipeline(req);
        if (ret != ESP_OK) {
            ESP_LOGD(TAG, "Middleware pipeline failed for %s", req->uri);
            wsm_update_stats_request(false);
            return ret;
        }
    }

    // Verificar autenticação se necessário
    if (route_entry->config.auth_level != WSM_USER_LEVEL_NONE) {
        ret = wsm_check_auth(req, route_entry->config.auth_level);
        if (ret != ESP_OK) {
            ESP_LOGD(TAG, "Authentication failed for %s", req->uri);
            wsm_update_stats_request(false);
            return ret;
        }
    }

    // Executar handler da rota
    ret = route_entry->config.handler(req);
    
    // Atualizar estatísticas
    wsm_update_stats_request(ret == ESP_OK);

    return ret;
}

// =============================================================================
// FUNÇÕES DE SUPORTE INTERNO
// =============================================================================

static esp_err_t wsm_ensure_spiffs_mounted(void)
{
    if (g_wsm_ctx.spiffs_mounted) {
        return ESP_OK;
    }

    esp_vfs_spiffs_conf_t conf = {
        .base_path = g_wsm_ctx.config.spiffs_mount_point,
        .partition_label = NULL,
        .max_files = 10,
        .format_if_mount_failed = false,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret == ESP_OK) {
        g_wsm_ctx.spiffs_mounted = true;
        ESP_LOGI(TAG, "SPIFFS mounted at %s", g_wsm_ctx.config.spiffs_mount_point);
    } else {
        ESP_LOGE(TAG, "Failed to mount SPIFFS: %s", esp_err_to_name(ret));
    }

    return ret;
}

static void wsm_update_stats_request(bool success)
{
    g_wsm_ctx.stats.requests_total++;
    if (success) {
        g_wsm_ctx.stats.requests_success++;
    } else {
        g_wsm_ctx.stats.requests_error++;
    }
}

// =============================================================================
// FUNÇÕES DE UTILITÁRIOS
// =============================================================================

esp_err_t wsm_send_json_response(httpd_req_t *req, const char *json_string)
{
    if (!req || !json_string) {
        return ESP_ERR_INVALID_ARG;
    }

    httpd_resp_set_type(req, "application/json");
    
    if (g_wsm_ctx.config.enable_cors) {
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    }
    
    return httpd_resp_send(req, json_string, strlen(json_string));
}

esp_err_t wsm_send_error_response(httpd_req_t *req, int code, const char *message)
{
    if (!req || !message) {
        return ESP_ERR_INVALID_ARG;
    }

    char status_str[32];
    snprintf(status_str, sizeof(status_str), "%d", code);

    httpd_resp_set_status(req, status_str);
    httpd_resp_set_type(req, "text/plain");
    
    return httpd_resp_send(req, message, strlen(message));
}

void wsm_url_decode_inplace(char *str)
{
    if (!str) return;
    
    char *src = str, *dst = str;
    while (*src) {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            char hex[3] = { src[1], src[2], '\0' };
            char val = (char) strtol(hex, NULL, 16);
            *dst++ = val;
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

void wsm_html_escape(const char *input, char *output, size_t output_size)
{
    if (!input || !output || output_size == 0) return;
    
    size_t oi = 0;
    for (size_t i = 0; input[i] != '\0' && oi + 6 < output_size; ++i) {
        unsigned char c = (unsigned char)input[i];
        switch (c) {
            case '&': if (oi + 5 < output_size) { strcpy(&output[oi], "&amp;"); oi += 5; } break;
            case '"': if (oi + 6 < output_size) { strcpy(&output[oi], "&quot;"); oi += 6; } break;
            case '<': if (oi + 4 < output_size) { strcpy(&output[oi], "&lt;"); oi += 4; } break;
            case '>': if (oi + 4 < output_size) { strcpy(&output[oi], "&gt;"); oi += 4; } break;
            default:
                output[oi++] = c;
        }
    }
    output[oi] = '\0';
}

// =============================================================================
// IMPLEMENTAÇÃO BÁSICA SERÁ CONTINUADA EM OUTROS ARQUIVOS
// =============================================================================

ESP_LOGI(TAG, "Web Server Manager core implementation loaded");