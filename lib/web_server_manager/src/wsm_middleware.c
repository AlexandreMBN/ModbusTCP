/**
 * @file wsm_middleware.c
 * @brief Sistema de middleware do Web Server Manager
 */

#include "wsm_middleware.h"
#include "web_server_manager.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "WSM_MIDDLEWARE";

// Declarações externas (definidas em web_server_manager.c)
extern wsm_context_t g_wsm_ctx;

// =============================================================================
// FUNÇÕES DE MIDDLEWARE
// =============================================================================

esp_err_t wsm_register_middleware(const wsm_middleware_t *middleware)
{
    if (!middleware || !middleware->handler) {
        return ESP_ERR_INVALID_ARG;
    }

    if (g_wsm_ctx.middleware_count >= WSM_MAX_MIDDLEWARES) {
        ESP_LOGE(TAG, "Maximum number of middlewares reached");
        return ESP_ERR_NO_MEM;
    }

    // Copiar configuração do middleware
    g_wsm_ctx.middlewares[g_wsm_ctx.middleware_count] = *middleware;
    g_wsm_ctx.middleware_count++;

    ESP_LOGI(TAG, "Middleware registered: type %d", middleware->type);
    return ESP_OK;
}

esp_err_t wsm_set_middleware_enabled(wsm_middleware_type_t type, bool enabled)
{
    for (size_t i = 0; i < g_wsm_ctx.middleware_count; i++) {
        if (g_wsm_ctx.middlewares[i].type == type) {
            g_wsm_ctx.middlewares[i].enabled = enabled;
            ESP_LOGI(TAG, "Middleware type %d %s", type, enabled ? "enabled" : "disabled");
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

bool wsm_is_middleware_enabled(wsm_middleware_type_t type)
{
    for (size_t i = 0; i < g_wsm_ctx.middleware_count; i++) {
        if (g_wsm_ctx.middlewares[i].type == type) {
            return g_wsm_ctx.middlewares[i].enabled;
        }
    }
    return false;
}

esp_err_t wsm_execute_middleware_pipeline(httpd_req_t *req)
{
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }

    // Executar middleware em ordem de registro
    for (size_t i = 0; i < g_wsm_ctx.middleware_count; i++) {
        wsm_middleware_t *middleware = &g_wsm_ctx.middlewares[i];
        
        if (!middleware->enabled) {
            continue;
        }

        ESP_LOGD(TAG, "Executing middleware type %d", middleware->type);
        
        esp_err_t ret = middleware->handler(req, middleware->user_data);
        if (ret != ESP_OK) {
            ESP_LOGD(TAG, "Middleware type %d failed: %s", middleware->type, esp_err_to_name(ret));
            return ret;
        }
    }

    return ESP_OK;
}

// =============================================================================
// MIDDLEWARE DE AUTENTICAÇÃO
// =============================================================================

esp_err_t wsm_auth_middleware(httpd_req_t *req, void *user_data)
{
    if (!req || !user_data) {
        return ESP_ERR_INVALID_ARG;
    }

    wsm_user_level_t *required_level = (wsm_user_level_t *)user_data;
    return wsm_check_auth(req, *required_level);
}

// =============================================================================
// MIDDLEWARE DE CORS
// =============================================================================

esp_err_t wsm_cors_middleware(httpd_req_t *req, void *user_data)
{
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }

    // Adicionar headers CORS
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, Authorization");
    httpd_resp_set_hdr(req, "Access-Control-Max-Age", "86400");

    // Se for requisição OPTIONS (preflight), responder imediatamente
    if (req->method == HTTP_OPTIONS) {
        httpd_resp_send(req, NULL, 0);
        return ESP_FAIL; // Parar pipeline - não executar handler principal
    }

    return ESP_OK;
}

// =============================================================================
// MIDDLEWARE DE LOGGING
// =============================================================================

esp_err_t wsm_logging_middleware(httpd_req_t *req, void *user_data)
{
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }

    // Obter informações da requisição
    const char *method_str = "";
    switch (req->method) {
        case HTTP_GET: method_str = "GET"; break;
        case HTTP_POST: method_str = "POST"; break;
        case HTTP_PUT: method_str = "PUT"; break;
        case HTTP_DELETE: method_str = "DELETE"; break;
        case HTTP_OPTIONS: method_str = "OPTIONS"; break;
        default: method_str = "UNKNOWN"; break;
    }

    // Obter User-Agent se disponível
    char user_agent[128] = "Unknown";
    size_t buf_len = httpd_req_get_hdr_value_len(req, "User-Agent") + 1;
    if (buf_len > 1 && buf_len <= sizeof(user_agent)) {
        httpd_req_get_hdr_value_str(req, "User-Agent", user_agent, buf_len);
    }

    // Obter IP do cliente (tentativa básica)
    char client_ip[32] = "Unknown";
    // Em ESP32, httpd não expõe diretamente o IP do cliente
    // Isso requereria modificações mais profundas no servidor HTTP

    ESP_LOGI(TAG, "[%s] %s %s - User-Agent: %.50s", 
             client_ip, method_str, req->uri, user_agent);

    return ESP_OK;
}

// =============================================================================
// MIDDLEWARE DE RATE LIMITING
// =============================================================================

typedef struct {
    uint32_t max_requests_per_minute;
    uint32_t window_size_seconds;
} wsm_rate_limit_config_t;

typedef struct {
    uint32_t requests_count;
    uint64_t window_start;
} wsm_rate_limit_state_t;

static wsm_rate_limit_state_t g_rate_limit_state = {0};

esp_err_t wsm_rate_limit_middleware(httpd_req_t *req, void *user_data)
{
    if (!req || !user_data) {
        return ESP_ERR_INVALID_ARG;
    }

    wsm_rate_limit_config_t *config = (wsm_rate_limit_config_t *)user_data;
    uint64_t now = esp_timer_get_time() / 1000000; // segundos

    // Verificar se precisa resetar janela
    if (now - g_rate_limit_state.window_start >= config->window_size_seconds) {
        g_rate_limit_state.requests_count = 0;
        g_rate_limit_state.window_start = now;
    }

    // Verificar limite
    if (g_rate_limit_state.requests_count >= config->max_requests_per_minute) {
        ESP_LOGW(TAG, "Rate limit exceeded: %d requests in window", 
                 g_rate_limit_state.requests_count);
        
        const char *error_msg = "Rate limit exceeded. Try again later.";
        httpd_resp_set_status(req, "429 Too Many Requests");
        httpd_resp_set_hdr(req, "Retry-After", "60");
        httpd_resp_send(req, error_msg, strlen(error_msg));
        
        return ESP_FAIL;
    }

    // Incrementar contador
    g_rate_limit_state.requests_count++;
    
    // Adicionar headers informativos
    char remaining_str[16];
    snprintf(remaining_str, sizeof(remaining_str), "%d", 
             config->max_requests_per_minute - g_rate_limit_state.requests_count);
    httpd_resp_set_hdr(req, "X-RateLimit-Remaining", remaining_str);

    return ESP_OK;
}

// =============================================================================
// FUNÇÕES DE CONFIGURAÇÃO DE MIDDLEWARE PADRÃO
// =============================================================================

esp_err_t wsm_setup_default_middleware(void)
{
    esp_err_t ret = ESP_OK;

    // Middleware de CORS
    if (g_wsm_ctx.config.enable_cors) {
        wsm_middleware_t cors_middleware = {
            .type = WSM_MIDDLEWARE_CORS,
            .handler = wsm_cors_middleware,
            .user_data = NULL,
            .enabled = true
        };
        ret = wsm_register_middleware(&cors_middleware);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register CORS middleware");
        }
    }

    // Middleware de logging
    if (g_wsm_ctx.config.enable_logging) {
        wsm_middleware_t logging_middleware = {
            .type = WSM_MIDDLEWARE_LOGGING,
            .handler = wsm_logging_middleware,
            .user_data = NULL,
            .enabled = true
        };
        ret = wsm_register_middleware(&logging_middleware);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register logging middleware");
        }
    }

    // Middleware de rate limiting (configuração básica)
    static wsm_rate_limit_config_t rate_limit_config = {
        .max_requests_per_minute = 100,
        .window_size_seconds = 60
    };
    
    wsm_middleware_t rate_limit_middleware = {
        .type = WSM_MIDDLEWARE_RATE_LIMIT,
        .handler = wsm_rate_limit_middleware,
        .user_data = &rate_limit_config,
        .enabled = false  // Desabilitado por padrão
    };
    ret = wsm_register_middleware(&rate_limit_middleware);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register rate limit middleware");
    }

    ESP_LOGI(TAG, "Default middleware setup completed");
    return ESP_OK;
}

// =============================================================================
// FUNÇÕES UTILITÁRIAS DE MIDDLEWARE
// =============================================================================

esp_err_t wsm_middleware_get_client_info(httpd_req_t *req, char *ip, size_t ip_size,
                                         char *user_agent, size_t ua_size)
{
    if (!req || !ip || !user_agent) {
        return ESP_ERR_INVALID_ARG;
    }

    // Inicializar com valores padrão
    strncpy(ip, "Unknown", ip_size - 1);
    ip[ip_size - 1] = '\0';
    
    strncpy(user_agent, "Unknown", ua_size - 1);
    user_agent[ua_size - 1] = '\0';

    // Tentar obter User-Agent
    size_t buf_len = httpd_req_get_hdr_value_len(req, "User-Agent") + 1;
    if (buf_len > 1 && buf_len <= ua_size) {
        httpd_req_get_hdr_value_str(req, "User-Agent", user_agent, buf_len);
    }

    // IP do cliente não é facilmente acessível no ESP-IDF httpd
    // Seria necessário modificações no servidor HTTP para obter essa informação

    return ESP_OK;
}

esp_err_t wsm_middleware_add_security_headers(httpd_req_t *req)
{
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }

    // Headers de segurança básicos
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "X-XSS-Protection", "1; mode=block");
    httpd_resp_set_hdr(req, "Referrer-Policy", "strict-origin-when-cross-origin");
    
    // Content Security Policy básica
    httpd_resp_set_hdr(req, "Content-Security-Policy", 
                       "default-src 'self'; script-src 'self' 'unsafe-inline'; "
                       "style-src 'self' 'unsafe-inline'; img-src 'self' data:");

    return ESP_OK;
}

esp_err_t wsm_middleware_cache_control(httpd_req_t *req, const char *cache_directive)
{
    if (!req || !cache_directive) {
        return ESP_ERR_INVALID_ARG;
    }

    return httpd_resp_set_hdr(req, "Cache-Control", cache_directive);
}