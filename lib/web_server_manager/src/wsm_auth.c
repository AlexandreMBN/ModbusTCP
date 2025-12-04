/**
 * @file wsm_auth.c
 * @brief Sistema de autenticação do Web Server Manager
 */

#include "web_server_manager.h"
#include <esp_log.h>
#include <esp_system.h>
#include <esp_random.h>
#include <nvs.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "WSM_AUTH";

// Declarações externas (definidas em web_server_manager.c)
extern wsm_context_t g_wsm_ctx;

// =============================================================================
// FUNÇÕES DE GERAÇÃO DE SESSÃO
// =============================================================================

static void wsm_generate_session_id(char *session_id, size_t size)
{
    if (!session_id || size < 16) return;
    
    uint32_t random_values[4];
    for (int i = 0; i < 4; i++) {
        random_values[i] = esp_random();
    }
    
    snprintf(session_id, size, "%08lx%08lx%08lx%08lx",
             random_values[0], random_values[1], random_values[2], random_values[3]);
}

// =============================================================================
// FUNÇÕES DE GERENCIAMENTO DE SESSÃO
// =============================================================================

static esp_err_t wsm_session_create(const char *session_id, wsm_user_level_t level)
{
    if (!session_id) {
        return ESP_ERR_INVALID_ARG;
    }

    // Procurar slot disponível
    for (int i = 0; i < 8; i++) {
        if (!g_wsm_ctx.sessions[i].valid) {
            wsm_session_t *session = &g_wsm_ctx.sessions[i];
            
            strncpy(session->session_id, session_id, sizeof(session->session_id) - 1);
            session->session_id[sizeof(session->session_id) - 1] = '\0';
            session->user_level = level;
            session->created_at = esp_timer_get_time() / 1000000;
            session->last_access = session->created_at;
            session->valid = true;
            
            ESP_LOGI(TAG, "Session created: %s (level: %d)", session_id, level);
            return ESP_OK;
        }
    }

    ESP_LOGW(TAG, "No available session slots");
    return ESP_ERR_NO_MEM;
}

static wsm_session_t *wsm_session_find(const char *session_id)
{
    if (!session_id) return NULL;

    for (int i = 0; i < 8; i++) {
        wsm_session_t *session = &g_wsm_ctx.sessions[i];
        if (session->valid && strcmp(session->session_id, session_id) == 0) {
            // Verificar se não expirou
            uint64_t now = esp_timer_get_time() / 1000000;
            if (now - session->last_access > (WSM_SESSION_TIMEOUT_MS / 1000)) {
                session->valid = false;
                ESP_LOGI(TAG, "Session expired: %s", session_id);
                return NULL;
            }
            
            session->last_access = now;
            return session;
        }
    }
    return NULL;
}

static void wsm_session_cleanup_expired(void)
{
    uint64_t now = esp_timer_get_time() / 1000000;
    
    for (int i = 0; i < 8; i++) {
        wsm_session_t *session = &g_wsm_ctx.sessions[i];
        if (session->valid && 
            now - session->last_access > (WSM_SESSION_TIMEOUT_MS / 1000)) {
            session->valid = false;
            ESP_LOGI(TAG, "Session cleaned up: %s", session->session_id);
        }
    }
}

// =============================================================================
// FUNÇÕES DE COOKIES
// =============================================================================

static esp_err_t wsm_get_session_id_from_cookie(httpd_req_t *req, char *session_id, size_t size)
{
    if (!req || !session_id || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Obter header Cookie
    size_t buf_len = httpd_req_get_hdr_value_len(req, "Cookie") + 1;
    if (buf_len <= 1) {
        return ESP_ERR_NOT_FOUND;
    }

    char *cookie_header = malloc(buf_len);
    if (!cookie_header) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = ESP_ERR_NOT_FOUND;
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie_header, buf_len) == ESP_OK) {
        // Procurar cookie da sessão
        char *cookie_start = strstr(cookie_header, WSM_SESSION_COOKIE_NAME "=");
        if (cookie_start) {
            cookie_start += strlen(WSM_SESSION_COOKIE_NAME "=");
            char *cookie_end = strchr(cookie_start, ';');
            
            size_t cookie_len = cookie_end ? (size_t)(cookie_end - cookie_start) : strlen(cookie_start);
            if (cookie_len > 0 && cookie_len < size) {
                strncpy(session_id, cookie_start, cookie_len);
                session_id[cookie_len] = '\0';
                ret = ESP_OK;
            }
        }
    }

    free(cookie_header);
    return ret;
}

static esp_err_t wsm_set_session_cookie(httpd_req_t *req, const char *session_id)
{
    if (!req || !session_id) {
        return ESP_ERR_INVALID_ARG;
    }

    char cookie_header[128];
    snprintf(cookie_header, sizeof(cookie_header), 
             "%s=%s; Path=/; HttpOnly; Max-Age=%d",
             WSM_SESSION_COOKIE_NAME, session_id, WSM_SESSION_TIMEOUT_MS / 1000);

    return httpd_resp_set_hdr(req, "Set-Cookie", cookie_header);
}

// =============================================================================
// FUNÇÕES PÚBLICAS DE AUTENTICAÇÃO
// =============================================================================

esp_err_t wsm_check_auth(httpd_req_t *req, wsm_user_level_t required_level)
{
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }

    if (required_level == WSM_USER_LEVEL_NONE) {
        return ESP_OK;
    }

    // Cleanup de sessões expiradas
    wsm_session_cleanup_expired();

    // Obter session ID do cookie
    char session_id[64];
    esp_err_t ret = wsm_get_session_id_from_cookie(req, session_id, sizeof(session_id));
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "No session cookie found");
        return wsm_send_auth_required_response(req);
    }

    // Procurar sessão
    wsm_session_t *session = wsm_session_find(session_id);
    if (!session) {
        ESP_LOGD(TAG, "Invalid session: %s", session_id);
        return wsm_send_auth_required_response(req);
    }

    // Verificar nível de acesso
    if (session->user_level < required_level) {
        ESP_LOGD(TAG, "Insufficient privileges: has %d, needs %d", 
                 session->user_level, required_level);
        return wsm_send_access_denied_response(req);
    }

    ESP_LOGD(TAG, "Authentication successful for session: %s", session_id);
    return ESP_OK;
}

esp_err_t wsm_set_user_level(httpd_req_t *req, wsm_user_level_t level)
{
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }

    // Gerar novo session ID
    char session_id[64];
    wsm_generate_session_id(session_id, sizeof(session_id));

    // Criar sessão
    esp_err_t ret = wsm_session_create(session_id, level);
    if (ret != ESP_OK) {
        return ret;
    }

    // Definir cookie
    return wsm_set_session_cookie(req, session_id);
}

wsm_user_level_t wsm_get_user_level(httpd_req_t *req)
{
    if (!req) {
        return WSM_USER_LEVEL_NONE;
    }

    char session_id[64];
    esp_err_t ret = wsm_get_session_id_from_cookie(req, session_id, sizeof(session_id));
    if (ret != ESP_OK) {
        return WSM_USER_LEVEL_NONE;
    }

    wsm_session_t *session = wsm_session_find(session_id);
    return session ? session->user_level : WSM_USER_LEVEL_NONE;
}

esp_err_t wsm_set_auth_credentials(const char *basic_user, const char *basic_pass,
                                   const char *admin_user, const char *admin_pass)
{
    if (!basic_user || !basic_pass || !admin_user || !admin_pass) {
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(g_wsm_ctx.basic_user, basic_user, sizeof(g_wsm_ctx.basic_user) - 1);
    g_wsm_ctx.basic_user[sizeof(g_wsm_ctx.basic_user) - 1] = '\0';
    
    strncpy(g_wsm_ctx.basic_pass, basic_pass, sizeof(g_wsm_ctx.basic_pass) - 1);
    g_wsm_ctx.basic_pass[sizeof(g_wsm_ctx.basic_pass) - 1] = '\0';
    
    strncpy(g_wsm_ctx.admin_user, admin_user, sizeof(g_wsm_ctx.admin_user) - 1);
    g_wsm_ctx.admin_user[sizeof(g_wsm_ctx.admin_user) - 1] = '\0';
    
    strncpy(g_wsm_ctx.admin_pass, admin_pass, sizeof(g_wsm_ctx.admin_pass) - 1);
    g_wsm_ctx.admin_pass[sizeof(g_wsm_ctx.admin_pass) - 1] = '\0';

    ESP_LOGI(TAG, "Authentication credentials updated");
    return ESP_OK;
}

// =============================================================================
// HANDLERS DE RESPOSTA DE AUTENTICAÇÃO
// =============================================================================

esp_err_t wsm_send_auth_required_response(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/login");
    return httpd_resp_send(req, NULL, 0);
}

esp_err_t wsm_send_access_denied_response(httpd_req_t *req)
{
    const char *html = 
        "<!DOCTYPE html>"
        "<html><head><title>Acesso Negado</title></head>"
        "<body><h1>Acesso Negado</h1>"
        "<p>Você não tem permissão para acessar este recurso.</p>"
        "<a href='/'>Voltar</a></body></html>";
    
    httpd_resp_set_status(req, "403 Forbidden");
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, strlen(html));
}

// =============================================================================
// PROCESSAMENTO DE LOGIN
// =============================================================================

esp_err_t wsm_process_login(const char *username, const char *password, wsm_user_level_t *level)
{
    if (!username || !password || !level) {
        return ESP_ERR_INVALID_ARG;
    }

    *level = WSM_USER_LEVEL_NONE;

    // Verificar credenciais básicas
    if (strcmp(username, g_wsm_ctx.basic_user) == 0 && 
        strcmp(password, g_wsm_ctx.basic_pass) == 0) {
        *level = WSM_USER_LEVEL_BASIC;
        ESP_LOGI(TAG, "Basic user authentication successful");
        return ESP_OK;
    }

    // Verificar credenciais de administrador
    if (strcmp(username, g_wsm_ctx.admin_user) == 0 && 
        strcmp(password, g_wsm_ctx.admin_pass) == 0) {
        *level = WSM_USER_LEVEL_ADMIN;
        ESP_LOGI(TAG, "Admin user authentication successful");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Authentication failed for user: %s", username);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t wsm_logout_session(httpd_req_t *req)
{
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }

    char session_id[64];
    esp_err_t ret = wsm_get_session_id_from_cookie(req, session_id, sizeof(session_id));
    if (ret == ESP_OK) {
        wsm_session_t *session = wsm_session_find(session_id);
        if (session) {
            session->valid = false;
            ESP_LOGI(TAG, "Session logged out: %s", session_id);
        }
    }

    // Limpar cookie
    char cookie_header[128];
    snprintf(cookie_header, sizeof(cookie_header), 
             "%s=; Path=/; HttpOnly; Max-Age=0", WSM_SESSION_COOKIE_NAME);
    httpd_resp_set_hdr(req, "Set-Cookie", cookie_header);

    return ESP_OK;
}