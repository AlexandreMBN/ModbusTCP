/**
 * @file wsm_handlers.c
 * @brief Handlers integrados do Web Server Manager
 */

#include "web_server_manager.h"
#include "wsm_templates.h"
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "WSM_HANDLERS";

// Declarações externas
extern wsm_context_t g_wsm_ctx;

// Protótipos de funções auxiliares
static esp_err_t wsm_extract_form_data(httpd_req_t *req, const char *key, char *value, size_t value_size);
static esp_err_t wsm_send_login_template(httpd_req_t *req, const char *error_message);

// =============================================================================
// HANDLER DE LOGIN
// =============================================================================

esp_err_t wsm_login_handler(httpd_req_t *req)
{
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Serving login page");
    return wsm_send_login_template(req, NULL);
}

esp_err_t wsm_login_post_handler(httpd_req_t *req)
{
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Processing login form");

    char username[64] = {0};
    char password[64] = {0};

    // Extrair dados do formulário
    esp_err_t ret = wsm_extract_form_data(req, "username", username, sizeof(username));
    if (ret != ESP_OK) {
        return wsm_send_login_template(req, "Nome de usuário é obrigatório");
    }

    ret = wsm_extract_form_data(req, "password", password, sizeof(password));
    if (ret != ESP_OK) {
        return wsm_send_login_template(req, "Senha é obrigatória");
    }

    // Processar login
    wsm_user_level_t user_level;
    ret = wsm_process_login(username, password, &user_level);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Login failed for user: %s", username);
        return wsm_send_login_template(req, "Nome de usuário ou senha incorretos");
    }

    // Definir nível do usuário na sessão
    ret = wsm_set_user_level(req, user_level);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create session");
        return wsm_send_login_template(req, "Erro interno do servidor");
    }

    // Redirecionar para página principal
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

esp_err_t wsm_logout_handler(httpd_req_t *req)
{
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Processing logout");

    // Limpar sessão
    wsm_logout_session(req);

    // Redirecionar para login
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/login");
    return httpd_resp_send(req, NULL, 0);
}

// =============================================================================
// HANDLER DE INFORMAÇÕES DO SISTEMA
// =============================================================================

esp_err_t wsm_system_info_handler(httpd_req_t *req)
{
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }

    // Preparar contexto do template
    wsm_template_context_t context;
    wsm_template_context_init(&context);

    // Informações do chip
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    char chip_model[32];
    switch (chip_info.model) {
        case CHIP_ESP32: strcpy(chip_model, "ESP32"); break;
        case CHIP_ESP32S2: strcpy(chip_model, "ESP32-S2"); break;
        case CHIP_ESP32C3: strcpy(chip_model, "ESP32-C3"); break;
        case CHIP_ESP32S3: strcpy(chip_model, "ESP32-S3"); break;
        default: strcpy(chip_model, "ESP32-Unknown"); break;
    }

    char revision_str[8];
    snprintf(revision_str, sizeof(revision_str), "%d", chip_info.revision);

    char cores_str[8];
    snprintf(cores_str, sizeof(cores_str), "%d", chip_info.cores);

    // Informações de memória
    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_free_heap = esp_get_minimum_free_heap_size();
    
    char free_heap_str[16];
    snprintf(free_heap_str, sizeof(free_heap_str), "%lu KB", free_heap / 1024);
    
    char min_heap_str[16];
    snprintf(min_heap_str, sizeof(min_heap_str), "%lu KB", min_free_heap / 1024);

    // Informações de flash
    uint32_t flash_size;
    esp_flash_get_size(NULL, &flash_size);
    char flash_size_str[16];
    snprintf(flash_size_str, sizeof(flash_size_str), "%lu MB", flash_size / (1024 * 1024));

    // Uptime
    uint64_t uptime_us = esp_timer_get_time();
    uint32_t uptime_days = uptime_us / (24ULL * 60 * 60 * 1000000);
    uint32_t uptime_hours = (uptime_us % (24ULL * 60 * 60 * 1000000)) / (60 * 60 * 1000000);
    uint32_t uptime_minutes = (uptime_us % (60ULL * 60 * 1000000)) / (60 * 1000000);

    char uptime_str[64];
    snprintf(uptime_str, sizeof(uptime_str), "%lu dias, %lu horas, %lu minutos", 
             uptime_days, uptime_hours, uptime_minutes);

    // Adicionar ao contexto
    wsm_template_add_substitution(&context, "CHIP_MODEL", chip_model);
    wsm_template_add_substitution(&context, "CHIP_REVISION", revision_str);
    wsm_template_add_substitution(&context, "CHIP_CORES", cores_str);
    wsm_template_add_substitution(&context, "FREE_HEAP", free_heap_str);
    wsm_template_add_substitution(&context, "MIN_FREE_HEAP", min_heap_str);
    wsm_template_add_substitution(&context, "FLASH_SIZE", flash_size_str);
    wsm_template_add_substitution(&context, "UPTIME", uptime_str);
    wsm_template_add_substitution(&context, "ESP_IDF_VERSION", esp_get_idf_version());

    // Informações do servidor web
    wsm_stats_t stats;
    wsm_get_stats(&stats);

    char requests_total_str[16];
    snprintf(requests_total_str, sizeof(requests_total_str), "%lu", stats.requests_total);
    
    char requests_success_str[16];
    snprintf(requests_success_str, sizeof(requests_success_str), "%lu", stats.requests_success);

    char requests_error_str[16];
    snprintf(requests_error_str, sizeof(requests_error_str), "%lu", stats.requests_error);

    wsm_template_add_substitution(&context, "REQUESTS_TOTAL", requests_total_str);
    wsm_template_add_substitution(&context, "REQUESTS_SUCCESS", requests_success_str);
    wsm_template_add_substitution(&context, "REQUESTS_ERROR", requests_error_str);

    // Renderizar template
    return wsm_respond_with_template(req, "system_info.html", &context);
}

// =============================================================================
// HANDLER DE REINICIALIZAÇÃO
// =============================================================================

esp_err_t wsm_system_restart_handler(httpd_req_t *req)
{
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "System restart requested");

    // Verificar método
    if (req->method == HTTP_POST) {
        // Enviar página de confirmação
        const char *html = 
            "<!DOCTYPE html>"
            "<html><head><title>Reiniciando Sistema</title>"
            "<meta http-equiv='refresh' content='10;url=/'></head>"
            "<body><h1>Reiniciando Sistema</h1>"
            "<p>O sistema está sendo reiniciado. Esta página será recarregada automaticamente em 10 segundos.</p>"
            "<p><a href='/'>Clique aqui se não for redirecionado automaticamente</a></p>"
            "</body></html>";

        httpd_resp_set_type(req, "text/html");
        esp_err_t ret = httpd_resp_send(req, html, strlen(html));

        // Agendar reinicialização após resposta
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();

        return ret;
    } else {
        // GET - Mostrar página de confirmação
        const char *html = 
            "<!DOCTYPE html>"
            "<html><head><title>Confirmar Reinicialização</title></head>"
            "<body><h1>Confirmar Reinicialização</h1>"
            "<p>Tem certeza que deseja reiniciar o sistema?</p>"
            "<form method='post'>"
            "<button type='submit'>Sim, Reiniciar</button> "
            "<a href='/' style='margin-left: 20px;'>Cancelar</a>"
            "</form></body></html>";

        httpd_resp_set_type(req, "text/html");
        return httpd_resp_send(req, html, strlen(html));
    }
}

// =============================================================================
// HANDLER DE FACTORY RESET
// =============================================================================

esp_err_t wsm_factory_reset_handler(httpd_req_t *req)
{
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Factory reset requested");

    // Verificar permissão de administrador
    esp_err_t auth_ret = wsm_check_auth(req, WSM_USER_LEVEL_ADMIN);
    if (auth_ret != ESP_OK) {
        return auth_ret;
    }

    // Verificar método
    if (req->method == HTTP_POST) {
        char confirm[16] = {0};
        wsm_extract_form_data(req, "confirm", confirm, sizeof(confirm));

        if (strcmp(confirm, "yes") != 0) {
            const char *html = 
                "<!DOCTYPE html>"
                "<html><head><title>Factory Reset Cancelado</title></head>"
                "<body><h1>Factory Reset Cancelado</h1>"
                "<p>A operação foi cancelada.</p>"
                "<a href='/'>Voltar</a></body></html>";

            httpd_resp_set_type(req, "text/html");
            return httpd_resp_send(req, html, strlen(html));
        }

        // Enviar página de confirmação
        const char *html = 
            "<!DOCTYPE html>"
            "<html><head><title>Executando Factory Reset</title>"
            "<meta http-equiv='refresh' content='15;url=/'></head>"
            "<body><h1>Executando Factory Reset</h1>"
            "<p>O sistema está sendo restaurado às configurações de fábrica.</p>"
            "<p>Esta página será recarregada automaticamente em 15 segundos.</p>"
            "</body></html>";

        httpd_resp_set_type(req, "text/html");
        esp_err_t ret = httpd_resp_send(req, html, strlen(html));

        // Executar factory reset após resposta
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        ESP_LOGI(TAG, "Performing factory reset...");
        nvs_flash_erase();
        
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();

        return ret;
    } else {
        // GET - Mostrar página de confirmação
        const char *html = 
            "<!DOCTYPE html>"
            "<html><head><title>Confirmar Factory Reset</title></head>"
            "<body><h1>Confirmar Factory Reset</h1>"
            "<p><strong>ATENÇÃO:</strong> Esta operação irá apagar todas as configurações e dados salvos no dispositivo.</p>"
            "<p>Esta ação não pode ser desfeita. Tem certeza que deseja continuar?</p>"
            "<form method='post'>"
            "<label><input type='checkbox' name='confirm' value='yes' required> "
            "Sim, eu entendo que todos os dados serão perdidos</label><br><br>"
            "<button type='submit'>Executar Factory Reset</button> "
            "<a href='/' style='margin-left: 20px;'>Cancelar</a>"
            "</form></body></html>";

        httpd_resp_set_type(req, "text/html");
        return httpd_resp_send(req, html, strlen(html));
    }
}

// =============================================================================
// FUNÇÕES AUXILIARES
// =============================================================================

static esp_err_t wsm_extract_form_data(httpd_req_t *req, const char *key, char *value, size_t value_size)
{
    if (!req || !key || !value || value_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Ler dados do POST
    if (req->content_len == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    char *buf = malloc(req->content_len + 1);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }

    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0) {
        free(buf);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    // Extrair valor
    esp_err_t result = httpd_query_key_value(buf, key, value, value_size);
    
    // URL decode
    if (result == ESP_OK) {
        wsm_url_decode_inplace(value);
    }

    free(buf);
    return result;
}

static esp_err_t wsm_send_login_template(httpd_req_t *req, const char *error_message)
{
    // Template de login simples
    const char *login_html = 
        "<!DOCTYPE html>"
        "<html lang='pt-BR'>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "<title>Login - Web Server Manager</title>"
        "<style>"
        "body { font-family: Arial, sans-serif; background: #f5f5f5; margin: 0; padding: 50px; }"
        ".login-container { max-width: 400px; margin: 0 auto; background: white; padding: 30px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }"
        ".login-header { text-align: center; margin-bottom: 30px; }"
        ".form-group { margin-bottom: 20px; }"
        "label { display: block; margin-bottom: 5px; font-weight: bold; }"
        "input[type='text'], input[type='password'] { width: 100%; padding: 10px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }"
        "button { width: 100%; padding: 12px; background: #007bff; color: white; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; }"
        "button:hover { background: #0056b3; }"
        ".error { background: #f8d7da; color: #721c24; padding: 10px; border-radius: 4px; margin-bottom: 20px; }"
        "</style>"
        "</head>"
        "<body>"
        "<div class='login-container'>"
        "<div class='login-header'>"
        "<h2>Web Server Manager</h2>"
        "<p>Faça login para acessar o sistema</p>"
        "</div>";

    char response[2048];
    int written = snprintf(response, sizeof(response), "%s", login_html);

    // Adicionar mensagem de erro se presente
    if (error_message) {
        written += snprintf(response + written, sizeof(response) - written,
                           "<div class='error'>%s</div>", error_message);
    }

    // Continuar com formulário
    written += snprintf(response + written, sizeof(response) - written,
                       "<form method='post' action='/login'>"
                       "<div class='form-group'>"
                       "<label for='username'>Usuário:</label>"
                       "<input type='text' id='username' name='username' required>"
                       "</div>"
                       "<div class='form-group'>"
                       "<label for='password'>Senha:</label>"
                       "<input type='password' id='password' name='password' required>"
                       "</div>"
                       "<button type='submit'>Entrar</button>"
                       "</form>"
                       "<div style='text-align: center; margin-top: 20px; color: #666; font-size: 12px;'>"
                       "Usuários padrão: adm/adm (básico) ou root/root (admin)"
                       "</div>"
                       "</div>"
                       "</body>"
                       "</html>");

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, response, written);
}

// =============================================================================
// FUNÇÕES DE REGISTRO DE HANDLERS PADRÃO
// =============================================================================

esp_err_t wsm_register_default_handlers(void)
{
    wsm_route_config_t default_routes[] = {
        {"/login", HTTP_GET, wsm_login_handler, WSM_USER_LEVEL_NONE, NULL, false},
        {"/login", HTTP_POST, wsm_login_post_handler, WSM_USER_LEVEL_NONE, NULL, false},
        {"/logout", HTTP_GET, wsm_logout_handler, WSM_USER_LEVEL_NONE, NULL, false},
        {"/system/info", HTTP_GET, wsm_system_info_handler, WSM_USER_LEVEL_BASIC, NULL, true},
        {"/system/restart", HTTP_GET, wsm_system_restart_handler, WSM_USER_LEVEL_ADMIN, NULL, true},
        {"/system/restart", HTTP_POST, wsm_system_restart_handler, WSM_USER_LEVEL_ADMIN, NULL, true},
        {"/system/factory-reset", HTTP_GET, wsm_factory_reset_handler, WSM_USER_LEVEL_ADMIN, NULL, true},
        {"/system/factory-reset", HTTP_POST, wsm_factory_reset_handler, WSM_USER_LEVEL_ADMIN, NULL, true}
    };

    esp_err_t ret = wsm_register_routes(default_routes, 
                                        sizeof(default_routes) / sizeof(default_routes[0]));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register default handlers");
        return ret;
    }

    ESP_LOGI(TAG, "Default handlers registered successfully");
    return ESP_OK;
}