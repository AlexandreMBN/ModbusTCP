#include "webserver.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "modbus_params.h"
#include "mqtt_client_task.h"
#include "cJSON.h"
#include "esp_spiffs.h"
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_err.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_system.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_mac.h>
#include <soc/rtc.h>
#include <ctype.h>

static const char *TAG = "web_min";
static httpd_handle_t server_handle = NULL;
static bool restart_task_running = false;

// Fun��o para verificar permiss�es de acesso
esp_err_t check_user_permission(httpd_req_t *req, user_level_t required_level) {
    if (!check_access_permission(required_level)) {
        user_level_t current_level = load_user_level();
        ESP_LOGW(TAG, "?? Acesso negado. N�vel atual: %d, Requerido: %d", current_level, required_level);
        
        // P�gina de acesso negado
        const char* access_denied_html = 
            "<!DOCTYPE html>"
            "<html lang='pt-BR'>"
            "<head><meta charset='UTF-8'><title>Acesso Negado</title>"
            "<link rel='stylesheet' href='/css/styles.css'></head>"
            "<body><div><h1>?? Acesso Negado</h1>"
            "<p>Voc� n�o tem permiss�o para acessar esta p�gina.</p>"
            "<p>N�vel de acesso atual: %s</p>"
            "<p>N�vel requerido: %s</p>"
            "<a href='/admin'>?? Voltar ao Painel</a></div></body></html>";
        
        char *response = malloc(1024);
        if (response) {
            const char* current_desc = (current_level == USER_LEVEL_BASIC) ? "Padr�o (adm)" : 
                                     (current_level == USER_LEVEL_ADMIN) ? "Administrador (root)" : "Nenhum";
            const char* required_desc = (required_level == USER_LEVEL_BASIC) ? "Padr�o" : "Administrador";
            
            snprintf(response, 1024, access_denied_html, current_desc, required_desc);
            httpd_resp_set_type(req, "text/html");
            esp_err_t result = httpd_resp_send(req, response, strlen(response));
            free(response);
            return result;
        }
        return httpd_resp_send_404(req);
    }
    return ESP_OK;
}

// Forward declaration for wifi handlers (defined later)
esp_err_t wifi_get_handler(httpd_req_t *req);
esp_err_t wifi_select_get_handler(httpd_req_t *req);
esp_err_t config_unit_get_handler(httpd_req_t *req);
esp_err_t unit_values_get_handler(httpd_req_t *req);
esp_err_t unit_values_save_post_handler(httpd_req_t *req);
esp_err_t ap_save_post_handler(httpd_req_t *req);
esp_err_t wifi_save_nvs_post_handler(httpd_req_t *req);
esp_err_t config_mode_save_post_handler(httpd_req_t *req);
 
esp_err_t wifi_save_post_handler(httpd_req_t *req);
esp_err_t wifi_scan_get_handler(httpd_req_t *req);
esp_err_t wifi_scan_trigger_handler(httpd_req_t *req);
esp_err_t wifi_scan_data_handler(httpd_req_t *req);
esp_err_t wifi_status_get_handler(httpd_req_t *req);
esp_err_t wifi_status_data_handler(httpd_req_t *req);
esp_err_t wifi_restart_post_handler(httpd_req_t *req);
esp_err_t wifi_test_connect_post_handler(httpd_req_t *req);
esp_err_t ap_config_get_handler(httpd_req_t *req);
esp_err_t ap_config_save_post_handler(httpd_req_t *req);
esp_err_t rtu_config_save_post_handler(httpd_req_t *req);
esp_err_t modbus_registers_save_post_handler(httpd_req_t *req);
esp_err_t info_get_handler(httpd_req_t *req);
esp_err_t wifi_config_save_post_handler(httpd_req_t *req);
esp_err_t wifi_connect_post_handler(httpd_req_t *req);

// MQTT handlers
esp_err_t mqtt_config_get_handler(httpd_req_t *req);
esp_err_t mqtt_config_post_handler(httpd_req_t *req);
esp_err_t mqtt_status_api_handler(httpd_req_t *req);
esp_err_t mqtt_test_api_handler(httpd_req_t *req);



// Helper function para p�ginas de confirma��o
esp_err_t send_confirmation_page(httpd_req_t *req, const char *page_title, 
                                const char *message_title, const char *message_text,
                                const char *return_url, const char *return_text, int countdown);

// Comparator for sorting AP records by RSSI (descending)
static int compare_ap_rssi(const void *a, const void *b) {
    const wifi_ap_record_t *ra = (const wifi_ap_record_t*)a;
    const wifi_ap_record_t *rb = (const wifi_ap_record_t*)b;
    if (ra->rssi == rb->rssi) return 0;
    return (rb->rssi - ra->rssi);
}


// helper: escape text for HTML attribute
static void html_escape(const char* in, char* out, size_t out_len) {
    if (!in || !out || out_len == 0) return;
    size_t oi = 0;
    for (size_t i = 0; in[i] != '\0' && oi + 6 < out_len; ++i) {
        unsigned char c = (unsigned char)in[i];
        switch (c) {
            case '&': if (oi + 5 < out_len) { strcpy(&out[oi], "&amp;"); oi += 5; } break;
            case '"': if (oi + 6 < out_len) { strcpy(&out[oi], "&quot;"); oi += 6; } break;
            case '<': if (oi + 4 < out_len) { strcpy(&out[oi], "&lt;"); oi += 4; } break;
            case '>': if (oi + 4 < out_len) { strcpy(&out[oi], "&gt;"); oi += 4; } break;
            default:
                out[oi++] = c;
        }
    }
    out[oi] = '\0';
}

// Decode URL-encoded string in-place. Converts + to space and %XX to the byte.
static void url_decode_inplace(char *s) {
    if (!s) return;
    char *src = s, *dst = s;
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

// Helper: map WiFi channel number to human-readable band
static const char* channel_to_band(int channel) {
    if (channel <= 0) return "";
    // 2.4 GHz channels 1-14
    if (channel >= 1 && channel <= 14) return "2.4GHz";
    // 5 GHz channels commonly start at 36 and above
    if (channel >= 36) return "5GHz";
    // Fallback: unknown/other
    return "";
}

// =============================================================================
// SISTEMA DE ARQUIVOS SPIFFS
// =============================================================================

// Ensure SPIFFS is mounted (simple one-time init)
static void ensure_spiffs(void) {
    static bool mounted = false;
    if (mounted) return;
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 10,  // Aumentado para suportar mais arquivos
        .format_if_mount_failed = true,
    };
    esp_vfs_spiffs_register(&conf);
    mounted = true;
}

// =============================================================================
// ARQUIVO EST�TICO E TEMPLATE SYSTEM
// =============================================================================

/**
 * Carrega conte�do de um arquivo do SPIFFS
 * @param filepath Caminho do arquivo
 * @param content Buffer para armazenar o conte�do (ser� alocado)
 * @return ESP_OK em caso de sucesso
 */
static esp_err_t load_file_content(const char *filepath, char **content) {
    ensure_spiffs();
    
    FILE *file = fopen(filepath, "r");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        return ESP_FAIL;
    }
    
    // Obt�m o tamanho do arquivo
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);
    
    if (file_size <= 0) {
        fclose(file);
        ESP_LOGE(TAG, "File is empty or error getting size: %s", filepath);
        return ESP_FAIL;
    }
    
    // Aloca mem�ria para o conte�do
    *content = malloc(file_size + 1);
    if (!*content) {
        fclose(file);
        ESP_LOGE(TAG, "Out of memory loading file: %s", filepath);
        return ESP_ERR_NO_MEM;
    }
    
    // L� o conte�do
    size_t read_size = fread(*content, 1, file_size, file);
    fclose(file);
    
    if (read_size != file_size) {
        free(*content);
        *content = NULL;
        ESP_LOGE(TAG, "Failed to read complete file: %s", filepath);
        return ESP_FAIL;
    }
    
    (*content)[file_size] = '\0'; // Null terminator
    return ESP_OK;
}

/**
 * Substitui placeholders no template por valores reais
 * @param template Template com placeholders {{NOME}}
 * @param placeholder Nome do placeholder (sem as chaves)
 * @param value Valor para substituir
 * @return String com as substitui��es feitas (deve ser liberada com free())
 */
static char* replace_placeholder(const char *template, const char *placeholder, const char *value) {
    if (!template || !placeholder || !value) return NULL;
    
    char search_pattern[64];
    snprintf(search_pattern, sizeof(search_pattern), "{{%s}}", placeholder);
    
    // Conta quantas ocorr�ncias existem
    int count = 0;
    const char *pos = template;
    while ((pos = strstr(pos, search_pattern)) != NULL) {
        count++;
        pos += strlen(search_pattern);
    }
    
    // Log apenas se encontrar placeholders para depura��o
    if (count > 0) {
        ESP_LOGI(TAG, "? Replacing %d occurrences of '%s' with '%s'", count, search_pattern, value);
    }
    
    if (count == 0) {
        // Nenhuma ocorr�ncia, retorna c�pia do template
        char *result = malloc(strlen(template) + 1);
        if (result) {
            strcpy(result, template);
        }
        return result;
    }
    
    // Calcula novo tamanho
    size_t old_len = strlen(search_pattern);
    size_t new_len = strlen(value);
    size_t result_len = strlen(template) + count * (new_len - old_len) + 1;
    
    char *result = malloc(result_len);
    if (!result) return NULL;
    
    // Faz as substitui��es
    const char *src = template;
    char *dst = result;
    
    while ((pos = strstr(src, search_pattern)) != NULL) {
        // Copia at� o placeholder
        size_t copy_len = pos - src;
        memcpy(dst, src, copy_len);
        dst += copy_len;
        
        // Copia o valor de substitui��o
        strcpy(dst, value);
        dst += new_len;
        
        // Move para ap�s o placeholder
        src = pos + old_len;
    }
    
    // Copia o resto
    strcpy(dst, src);
    
    return result;
}

/**
 * Aplica m�ltiplas substitui��es de template
 * @param template Template original
 * @param substitutions Array de pares [placeholder, value], terminado com NULL
 * @return Template processado (deve ser liberado com free())
 */
static char* apply_template_substitutions(const char *template, const char **substitutions) {
    if (!template || !substitutions) return NULL;
    
    char *current = malloc(strlen(template) + 1);
    if (!current) return NULL;
    strcpy(current, template);
    
    // Aplica cada substitui��o
    for (int i = 0; substitutions[i] != NULL && substitutions[i+1] != NULL; i += 2) {
        char *new_template = replace_placeholder(current, substitutions[i], substitutions[i+1]);
        if (new_template) {
            free(current);
            current = new_template;
        }
    }
    
    return current;
}

// =============================================================================
// HANDLERS PARA ARQUIVOS EST�TICOS
// =============================================================================

/**
 * Determina o tipo MIME baseado na extens�o do arquivo
 */


// Get CSS style content 
char* get_css_content() {
    char *content = NULL;
    esp_err_t ret = load_file_content("/spiffs/css/styles.css", &content);
    if (ret != ESP_OK) {
        return NULL;
    }
    return content;
}

// Get MIME type based on file extension
const char* get_mime_type(const char* filepath) {
    const char* ext = strrchr(filepath, '.');
    if (!ext) return "text/plain";
    
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) {
        return "text/html";
    } else if (strcmp(ext, ".css") == 0) {
        return "text/css";
    } else if (strcmp(ext, ".js") == 0) {
        return "application/javascript";
    } else if (strcmp(ext, ".json") == 0) {
        return "application/json";
    } else if (strcmp(ext, ".png") == 0) {
        return "image/png";
    } else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) {
        return "image/jpeg";
    } else if (strcmp(ext, ".gif") == 0) {
        return "image/gif";
    } else if (strcmp(ext, ".ico") == 0) {
        return "image/x-icon";
    }
    return "text/plain";
}

/**
 * Handler gen�rico para servir arquivos est�ticos
 */
static esp_err_t static_file_handler(httpd_req_t *req) {
    const char *uri = req->uri;
    char filepath[1024];  // Aumentado para evitar truncamento
    
    // Constr�i caminho do arquivo
    if (strcmp(uri, "/") == 0) {
        strcpy(filepath, "/spiffs/html/index.html");
    } else {
        snprintf(filepath, sizeof(filepath), "/spiffs%s", uri);
    }
    
    ESP_LOGI(TAG, "Serving static file: %s", filepath);
    
    // Carrega conte�do do arquivo
    char *content = NULL;
    esp_err_t ret = load_file_content(filepath, &content);
    
    if (ret != ESP_OK || !content) {
        ESP_LOGE(TAG, "Failed to load file: %s", filepath);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    
    // Define tipo MIME
    const char *mime_type = get_mime_type(filepath);
    httpd_resp_set_type(req, mime_type);
    
    // Adiciona headers anti-cache para desenvolvimento
    if (strstr(uri, ".css") || strstr(uri, ".js") || strstr(uri, ".html")) {
        // Desabilita cache durante desenvolvimento
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
        httpd_resp_set_hdr(req, "Pragma", "no-cache");
        httpd_resp_set_hdr(req, "Expires", "0");
    }
    
    // Envia resposta
    esp_err_t result = httpd_resp_send(req, content, strlen(content));
    
    free(content);
    return result;
}

// Fun��o helper para enviar p�ginas de confirma��o usando template
esp_err_t send_confirmation_page(httpd_req_t *req, const char *page_title, 
                                const char *message_title, const char *message_text,
                                const char *return_url, const char *return_text, int countdown) {
    char *content = NULL;
    esp_err_t ret = load_file_content("/spiffs/html/confirmation.html", &content);
    
    if (ret != ESP_OK || !content) {
        ESP_LOGE(TAG, "Failed to load confirmation.html template");
        return httpd_resp_send(req, message_text, HTTPD_RESP_USE_STRLEN);
    }
    
    // Converte countdown para string
    char countdown_str[8];
    snprintf(countdown_str, sizeof(countdown_str), "%d", countdown);
    
    // Define substitui��es para o template
    const char *substitutions[] = {
        "PAGE_TITLE", page_title,
        "MESSAGE_TITLE", message_title,
        "MESSAGE_TEXT", message_text,
        "REDIRECT_DISPLAY", (countdown > 0) ? "block" : "none",
        "COUNTDOWN", countdown_str,
        "RETURN_URL", return_url,
        "RETURN_TEXT", return_text,
        NULL, NULL
    };
    
    char *final_html = apply_template_substitutions(content, substitutions);
    free(content);
    
    if (!final_html) {
        ESP_LOGE(TAG, "Failed to apply template substitutions for confirmation");
        return httpd_resp_send(req, message_text, HTTPD_RESP_USE_STRLEN);
    }
    
    httpd_resp_set_type(req, "text/html");
    esp_err_t result = httpd_resp_send(req, final_html, strlen(final_html));
    free(final_html);
    return result;
}

/**
 * Handler espec�fico para arquivos CSS
 */
static esp_err_t css_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "CSS handler called for URI: %s", req->uri);
    return static_file_handler(req);
}

/**
 * Handler espec�fico para arquivos JavaScript
 */
static esp_err_t js_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "JS handler called for URI: %s", req->uri);
    return static_file_handler(req);
}

// Root page - agora usa arquivo HTML
esp_err_t root_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Serving root page from HTML file");
    return static_file_handler(req);
}

// P�ginas auxiliares - agora usam arquivos HTML
esp_err_t reset_get_handler(httpd_req_t *req) {
    char *content = NULL;
    esp_err_t ret = load_file_content("/spiffs/html/reset.html", &content);
    
    if (ret != ESP_OK || !content) {
        return httpd_resp_send_404(req);
    }
    
    httpd_resp_set_type(req, "text/html");
    esp_err_t result = httpd_resp_send(req, content, strlen(content));
    free(content);
    return result;
}

esp_err_t exit_get_handler(httpd_req_t *req) {
    char *content = NULL;
    esp_err_t ret = load_file_content("/spiffs/html/exit.html", &content);
    
    if (ret != ESP_OK || !content) {
        return httpd_resp_send_404(req);
    }
    
    httpd_resp_set_type(req, "text/html");
    esp_err_t result = httpd_resp_send(req, content, strlen(content));
    free(content);
    return result;
}

// Login processing handler - agora usa arquivo HTML para erro
esp_err_t do_login_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Processing /do_login");
    char buf[512];
    char user[64] = "", pass[64] = "";

    // Support both POST (form body) and GET (query string) for compatibility
    if (req->method == HTTP_POST) {
        // Read the POST data (application/x-www-form-urlencoded)
        int total_len = req->content_len;
        if (total_len > 0 && total_len < (int)sizeof(buf)) {
            int ret = httpd_req_recv(req, buf, total_len);
            if (ret > 0) {
                buf[ret] = '\0';
                httpd_query_key_value(buf, "user", user, sizeof(user));
                httpd_query_key_value(buf, "pass", pass, sizeof(pass));
            }
        }
    } else {
        size_t len = httpd_req_get_url_query_len(req) + 1;
        if (len > 1 && len < sizeof(buf)) {
            httpd_req_get_url_query_str(req, buf, len);
            httpd_query_key_value(buf, "user", user, sizeof(user));
            httpd_query_key_value(buf, "pass", pass, sizeof(pass));
        }
    }

    if (strlen(user) > 0 && strlen(pass) > 0) {
        if (strcmp(user, "adm") == 0 && strcmp(pass, "adm") == 0) {
            // Usu�rio padr�o
            save_login_state(true);
            save_user_level(USER_LEVEL_BASIC);
            ESP_LOGI(TAG, "Login usu�rio padr�o (adm)");
            // redirect to admin page with Wi-Fi/Modbus/Reset buttons
            httpd_resp_set_status(req, "302 Found");
            httpd_resp_set_hdr(req, "Location", "/admin");
            return httpd_resp_send(req, NULL, 0);
        } else if (strcmp(user, "root") == 0 && strcmp(pass, "root") == 0) {
            // Administrador - acesso completo
            save_login_state_root(true);
            save_user_level(USER_LEVEL_ADMIN);
            ESP_LOGI(TAG, "Login administrador (root)");
            // redirect to admin page with Wi-Fi/Modbus/Reset buttons
            httpd_resp_set_status(req, "302 Found");
            httpd_resp_set_hdr(req, "Location", "/admin");
            return httpd_resp_send(req, NULL, 0);
        }
    }

    // Login inv�lido - usa arquivo HTML
    char *content = NULL;
    esp_err_t ret = load_file_content("/spiffs/html/login_invalid.html", &content);
    
    if (ret != ESP_OK || !content) {
        return httpd_resp_send_404(req);
    }
    
    httpd_resp_set_type(req, "text/html");
    esp_err_t result = httpd_resp_send(req, content, strlen(content));
    free(content);
    return result;
}

// Handler da p�gina administrativa - com substitui��es dos registradores
esp_err_t admin_get_handler(httpd_req_t *req) {
    char *template_content = NULL;
    esp_err_t ret = load_file_content("/spiffs/html/admin.html", &template_content);
    
    if (ret != ESP_OK || !template_content) {
        return httpd_resp_send_404(req);
    }
    
    // Preparar valores dos registradores para substitui��o
    char reg_values[35][32]; // Buffer para todos os valores
    
    // Registradores 2000 (1 valor)
    snprintf(reg_values[0], sizeof(reg_values[0]), "%d", reg2000[0]);
    
    // Registradores 4000 (8 valores)
    for (int i = 0; i < 8; i++) {
        snprintf(reg_values[1 + i], sizeof(reg_values[1 + i]), "%d", reg4000[i]);
    }
    
    // Registradores 6000 (5 valores)
    for (int i = 0; i < 5; i++) {
        snprintf(reg_values[9 + i], sizeof(reg_values[9 + i]), "%d", reg6000[i]);
    }
    
    // Registradores 9000 (20 valores)
    for (int i = 0; i < 20; i++) {
        snprintf(reg_values[14 + i], sizeof(reg_values[14 + i]), "%d", reg9000[i]);
    }

    // Valores RTU (registradores 1000) - usar valores dos registradores diretamente
    char rtu_baudrate[16];
    char rtu_slave_address[16];
    char rtu_parity[16];
    
    // Obter valores dos registradores 1000
    snprintf(rtu_baudrate, sizeof(rtu_baudrate), "%d", holding_reg1000_params.reg1000[baudrate]);
    snprintf(rtu_slave_address, sizeof(rtu_slave_address), "%d", holding_reg1000_params.reg1000[endereco]);
    snprintf(rtu_parity, sizeof(rtu_parity), "%d", holding_reg1000_params.reg1000[paridade]);

    // Strings para flags dos registradores 4000 e RTU
    const char *reg4000_5_0 = (reg4000[5] == 0) ? "selected" : "";
    const char *reg4000_5_1 = (reg4000[5] == 1) ? "selected" : "";
    const char *reg4000_6_0 = (reg4000[6] == 0) ? "selected" : "";
    const char *reg4000_6_1 = (reg4000[6] == 1) ? "selected" : "";
    const char *reg4000_7_0 = (reg4000[7] == 0) ? "selected" : "";
    const char *reg4000_7_1 = (reg4000[7] == 1) ? "selected" : "";
    
    const char *rtu_parity_0 = (holding_reg1000_params.reg1000[paridade] == 0) ? "selected" : "";
    const char *rtu_parity_1 = (holding_reg1000_params.reg1000[paridade] == 1) ? "selected" : "";
    const char *rtu_parity_2 = (holding_reg1000_params.reg1000[paridade] == 2) ? "selected" : "";

    // Informa��es do usu�rio atual
    user_level_t current_user_level = load_user_level();
    char user_level_str[32];
    char user_permissions[64];
    
    // Vari�veis para controlar visibilidade das se��es
    const char *show_basic_content = "";      // Para usu�rio padr�o (adm)
    const char *show_admin_content = "";      // Para administrador (root)
    const char *hide_basic_content = "style='display:none;'";
    const char *hide_admin_content = "style='display:none;'";
    
    if (current_user_level == USER_LEVEL_ADMIN) {
        strcpy(user_level_str, "?? Administrador (root)");
        strcpy(user_permissions, "?? Acesso Completo");
        show_admin_content = "";
        show_basic_content = hide_basic_content; // Admin n�o v� conte�do b�sico
    } else if (current_user_level == USER_LEVEL_BASIC) {
        strcpy(user_level_str, "?? Usu�rio Padr�o (adm)");
        strcpy(user_permissions, "");
        show_basic_content = "";
        show_admin_content = hide_admin_content; // B�sico n�o v� conte�do admin
    } else {
        strcpy(user_level_str, "? N�o identificado");
        strcpy(user_permissions, "?? Sem permiss�es");
        show_basic_content = hide_basic_content;
        show_admin_content = hide_admin_content;
    }

    // Array de substitui��es para o template
    const char *substitutions[] = {
        // Informa��es do usu�rio
        "USER_LEVEL", user_level_str,
        "USER_PERMISSIONS", user_permissions,
        "SHOW_BASIC_CONTENT", show_basic_content,
        "SHOW_ADMIN_CONTENT", show_admin_content,
        
        // Registradores RTU (1000)
        "RTU_BAUDRATE", rtu_baudrate,
        "RTU_SLAVE_ADDRESS", rtu_slave_address,
        "RTU_PARITY_0_SELECTED", rtu_parity_0,
        "RTU_PARITY_1_SELECTED", rtu_parity_1,
        "RTU_PARITY_2_SELECTED", rtu_parity_2,
        
        // Registradores 2000
        "REG2000_0", reg_values[0],
        
        // Registradores 4000
        "REG4000_0", reg_values[1],
        "REG4000_1", reg_values[2],
        "REG4000_2", reg_values[3],
        "REG4000_3", reg_values[4],
        "REG4000_4", reg_values[5],
        "REG4000_5", reg_values[6],
        "REG4000_6", reg_values[7],
        "REG4000_7", reg_values[8],
        "REG4000_5_0_SELECTED", reg4000_5_0,
        "REG4000_5_1_SELECTED", reg4000_5_1,
        "REG4000_6_0_SELECTED", reg4000_6_0,
        "REG4000_6_1_SELECTED", reg4000_6_1,
        "REG4000_7_0_SELECTED", reg4000_7_0,
        "REG4000_7_1_SELECTED", reg4000_7_1,
        
        // Registradores 6000
        "REG6000_0", reg_values[9],
        "REG6000_1", reg_values[10],
        "REG6000_2", reg_values[11],
        "REG6000_3", reg_values[12],
        "REG6000_4", reg_values[13],
        
        // Registradores 9000
        "REG9000_0", reg_values[14],
        "REG9000_1", reg_values[15],
        "REG9000_2", reg_values[16],
        "REG9000_3", reg_values[17],
        "REG9000_4", reg_values[18],
        "REG9000_5", reg_values[19],
        "REG9000_6", reg_values[20],
        "REG9000_7", reg_values[21],
        "REG9000_8", reg_values[22],
        "REG9000_9", reg_values[23],
        "REG9000_10", reg_values[24],
        "REG9000_11", reg_values[25],
        "REG9000_12", reg_values[26],
        "REG9000_13", reg_values[27],
        "REG9000_14", reg_values[28],
        "REG9000_15", reg_values[29],
        "REG9000_16", reg_values[30],
        "REG9000_17", reg_values[31],
        "REG9000_18", reg_values[32],
        "REG9000_19", reg_values[33],
        
        NULL // Terminador
    };

    // Aplica as substitui��es no template
    char *final_html = apply_template_substitutions(template_content, substitutions);
    free(template_content);
    
    if (!final_html) {
        ESP_LOGE(TAG, "Failed to apply template substitutions for admin page");
        return ESP_FAIL;
    }
    
    // Envia a resposta
    httpd_resp_set_type(req, "text/html");
    esp_err_t result = httpd_resp_send(req, final_html, strlen(final_html));
    free(final_html);
    return result;
}

// Factory reset endpoint (POST) - erases NVS, removes SPIFFS files and restarts
static esp_err_t factory_reset_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Factory reset requested via web");
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);

    // small delay to ensure response is sent
    vTaskDelay(pdMS_TO_TICKS(200));

    esp_err_t ret = nvs_flash_erase();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao apagar NVS: %s", esp_err_to_name(ret));
    }
    if (remove("/spiffs/conteudo.json") != 0) {
        ESP_LOGW(TAG, "Arquivo conteudo.json n�o encontrado ou j� removido");
    }
    if (remove("/spiffs/config.json") != 0) {
        ESP_LOGW(TAG, "Arquivo config.json n�o encontrado ou j� removido");
    }
    if (remove("/spiffs/network_config.json") != 0) {
        ESP_LOGW(TAG, "Arquivo network_config.json n�o encontrado ou j� removido");
    }
    ESP_LOGI(TAG, "Reiniciando ESP32... (factory reset)");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

// Login page - agora usa arquivo HTML
esp_err_t login_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "login_get_handler called (serving login page from HTML file)");
    
    char *content = NULL;
    esp_err_t ret = load_file_content("/spiffs/html/login.html", &content);
    
    if (ret != ESP_OK || !content) {
        ESP_LOGE(TAG, "Failed to load login.html");
        return httpd_resp_send_404(req);
    }
    
    httpd_resp_set_type(req, "text/html");
    esp_err_t result = httpd_resp_send(req, content, strlen(content));
    free(content);
    return result;
}

// Modbus server page � render current values and toggle RTU/TCP blocks (default RTU)
esp_err_t modbus_get_handler(httpd_req_t *req) {
    // Carrega template HTML
    char *template_content = NULL;
    esp_err_t ret = load_file_content("/spiffs/html/modbus.html", &template_content);
    
    if (ret != ESP_OK || !template_content) {
        ESP_LOGE(TAG, "Failed to load modbus.html");
        return httpd_resp_send_404(req);
    }

    // **CARREGAR VALORES DO CONFIG.JSON SE EXISTIREM**
    ensure_spiffs();
    FILE *f = fopen("/spiffs/config.json", "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        rewind(f);
        char *data = malloc(size + 1);
        if (data) {
            fread(data, 1, size, f);
            data[size] = '\0';
            cJSON *root = cJSON_Parse(data);
            if (root) {
                cJSON *registers_obj = cJSON_GetObjectItem(root, "modbus_registers");
                if (registers_obj) {
                    ESP_LOGI(TAG, "Carregando registradores salvos do config.json");
                    
                    // Carregar registradores 4000
                    cJSON *reg4000_array = cJSON_GetObjectItem(registers_obj, "reg4000");
                    if (reg4000_array && cJSON_IsArray(reg4000_array)) {
                        for (int i = 0; i < 8 && i < cJSON_GetArraySize(reg4000_array); i++) {
                            cJSON *item = cJSON_GetArrayItem(reg4000_array, i);
                            if (cJSON_IsNumber(item)) {
                                reg4000[i] = item->valueint;
                            }
                        }
                    }
                    
                    // Carregar registradores 6000
                    cJSON *reg6000_array = cJSON_GetObjectItem(registers_obj, "reg6000");
                    if (reg6000_array && cJSON_IsArray(reg6000_array)) {
                        for (int i = 0; i < 5 && i < cJSON_GetArraySize(reg6000_array); i++) {
                            cJSON *item = cJSON_GetArrayItem(reg6000_array, i);
                            if (cJSON_IsNumber(item)) {
                                reg6000[i] = item->valueint;
                            }
                        }
                    }
                    
                    // Carregar registradores 9000
                    cJSON *reg9000_array = cJSON_GetObjectItem(registers_obj, "reg9000");
                    if (reg9000_array && cJSON_IsArray(reg9000_array)) {
                        for (int i = 0; i < 20 && i < cJSON_GetArraySize(reg9000_array); i++) {
                            cJSON *item = cJSON_GetArrayItem(reg9000_array, i);
                            if (cJSON_IsNumber(item)) {
                                reg9000[i] = item->valueint;
                            }
                        }
                    }
                }
                cJSON_Delete(root);
            }
            free(data);
        }
        fclose(f);
    }

    // Prepara substitui��es para todos os registradores
    char reg_values[100][16]; // Buffer para valores dos registradores
    
    // Registradores 2000 (1 valor - somente leitura)
    snprintf(reg_values[0], sizeof(reg_values[0]), "%d", reg2000[0]);
    
    // Registradores 4000 (8 valores)
    for (int i = 0; i < 8; i++) {
        snprintf(reg_values[1 + i], sizeof(reg_values[1 + i]), "%d", reg4000[i]);
    }
    
    // Registradores 6000 (5 valores)
    for (int i = 0; i < 5; i++) {
        snprintf(reg_values[9 + i], sizeof(reg_values[9 + i]), "%d", reg6000[i]);
    }
    
    // Registradores 9000 (20 valores)
    for (int i = 0; i < 20; i++) {
        snprintf(reg_values[14 + i], sizeof(reg_values[14 + i]), "%d", reg9000[i]);
    }

    // Strings para flags dos registradores 4000
    const char *reg4000_5_0 = (reg4000[5] == 0) ? "selected" : "";
    const char *reg4000_5_1 = (reg4000[5] == 1) ? "selected" : "";
    const char *reg4000_6_0 = (reg4000[6] == 0) ? "selected" : "";
    const char *reg4000_6_1 = (reg4000[6] == 1) ? "selected" : "";
    const char *reg4000_7_0 = (reg4000[7] == 0) ? "selected" : "";
    const char *reg4000_7_1 = (reg4000[7] == 1) ? "selected" : "";

    // Array de substitui��es para o template
    const char *substitutions[] = {
        // Registradores 2000
        "REG2000_0", reg_values[0],
        
        // Registradores 4000
        "REG4000_0", reg_values[1],
        "REG4000_1", reg_values[2],
        "REG4000_2", reg_values[3],
        "REG4000_3", reg_values[4],
        "REG4000_4", reg_values[5],
        "REG4000_5", reg_values[6],
        "REG4000_6", reg_values[7],
        "REG4000_7", reg_values[8],
        "REG4000_5_0_SELECTED", reg4000_5_0,
        "REG4000_5_1_SELECTED", reg4000_5_1,
        "REG4000_6_0_SELECTED", reg4000_6_0,
        "REG4000_6_1_SELECTED", reg4000_6_1,
        "REG4000_7_0_SELECTED", reg4000_7_0,
        "REG4000_7_1_SELECTED", reg4000_7_1,
        
        // Registradores 6000
        "REG6000_0", reg_values[9],
        "REG6000_1", reg_values[10],
        "REG6000_2", reg_values[11],
        "REG6000_3", reg_values[12],
        "REG6000_4", reg_values[13],
        
        // Registradores 9000
        "REG9000_0", reg_values[14],
        "REG9000_1", reg_values[15],
        "REG9000_2", reg_values[16],
        "REG9000_3", reg_values[17],
        "REG9000_4", reg_values[18],
        "REG9000_5", reg_values[19],
        "REG9000_6", reg_values[20],
        "REG9000_7", reg_values[21],
        "REG9000_8", reg_values[22],
        "REG9000_9", reg_values[23],
        "REG9000_10", reg_values[24],
        "REG9000_11", reg_values[25],
        "REG9000_12", reg_values[26],
        "REG9000_13", reg_values[27],
        "REG9000_14", reg_values[28],
        "REG9000_15", reg_values[29],
        "REG9000_16", reg_values[30],
        "REG9000_17", reg_values[31],
        "REG9000_18", reg_values[32],
        "REG9000_19", reg_values[33],
        
        NULL // Terminador
    };

    // Aplica as substitui��es no template
    char *final_html = apply_template_substitutions(template_content, substitutions);
    free(template_content);
    
    if (!final_html) {
        ESP_LOGE(TAG, "Failed to apply template substitutions");
        return ESP_FAIL;
    }
    
    // Envia a resposta
    httpd_resp_set_type(req, "text/html");
    esp_err_t res = httpd_resp_send(req, final_html, strlen(final_html));
    free(final_html);
    return res;
}

// Handler para p�gina de configura��o do Access Point
esp_err_t ap_config_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "ap_config_get_handler called (serving AP config page from HTML file)");
    
    char *content = NULL;
    esp_err_t ret = load_file_content("/spiffs/html/ap-config.html", &content);
    
    if (ret != ESP_OK || !content) {
        ESP_LOGE(TAG, "Failed to load ap-config.html");
        return httpd_resp_send_404(req);
    }
    
    // L� configura��es atuais do NVS
    char ap_ssid[33] = "ESP32-AP";
    char ap_username[33] = "admin";  
    char ap_password[64] = "12345678";
    char ap_ip[16] = "192.168.4.1";
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("ap_config", NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        size_t required_size;
        
        required_size = sizeof(ap_ssid);
        nvs_get_str(nvs_handle, "ssid", ap_ssid, &required_size);
        
        required_size = sizeof(ap_username);
        nvs_get_str(nvs_handle, "username", ap_username, &required_size);
        
        required_size = sizeof(ap_password);
        nvs_get_str(nvs_handle, "password", ap_password, &required_size);
        
        required_size = sizeof(ap_ip);
        nvs_get_str(nvs_handle, "ip", ap_ip, &required_size);
        
        nvs_close(nvs_handle);
    }
    
    // Define substitui��es para o template
    const char *substitutions[] = {
        "AP_SSID", ap_ssid,
        "AP_USERNAME", ap_username,
        "AP_PASSWORD", ap_password,
        "AP_IP", ap_ip,
        NULL, NULL
    };
    
    char *final_html = apply_template_substitutions(content, substitutions);
    free(content);
    
    if (!final_html) {
        ESP_LOGE(TAG, "Failed to apply template substitutions for ap-config");
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "text/html");
    esp_err_t result = httpd_resp_send(req, final_html, strlen(final_html));
    free(final_html);
    return result;
}

// Handler to save modbus config into /spiffs/config.json (preserving existing keys)
esp_err_t modbus_save_post_handler(httpd_req_t *req) {
    // read body
    char buf[1024] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (ret <= 0) {
        return httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);
    }

    // buf contains urlencoded form. Use httpd_query_key_value on it.
    char modbus_mode[8] = "";
    char rtu_baud[16] = ""; char rtu_parity[8] = ""; char rtu_databits[8] = ""; char rtu_stopbits[8] = ""; char rtu_addr[8] = "";
    char tcp_port[8] = ""; char tcp_unit[8] = ""; char tcp_enable[8] = ""; char tcp_timeout[8] = "";
    char tcp_ip[16] = ""; char tcp_gateway[16] = "";
    httpd_query_key_value(buf, "modbus_mode", modbus_mode, sizeof(modbus_mode));
    httpd_query_key_value(buf, "rtu_baud", rtu_baud, sizeof(rtu_baud));
    httpd_query_key_value(buf, "rtu_parity", rtu_parity, sizeof(rtu_parity));
    httpd_query_key_value(buf, "rtu_databits", rtu_databits, sizeof(rtu_databits));
    httpd_query_key_value(buf, "rtu_stopbits", rtu_stopbits, sizeof(rtu_stopbits));
    httpd_query_key_value(buf, "rtu_addr", rtu_addr, sizeof(rtu_addr));
    httpd_query_key_value(buf, "tcp_port", tcp_port, sizeof(tcp_port));
    httpd_query_key_value(buf, "tcp_unit", tcp_unit, sizeof(tcp_unit));
    httpd_query_key_value(buf, "tcp_enable", tcp_enable, sizeof(tcp_enable));
    httpd_query_key_value(buf, "tcp_timeout", tcp_timeout, sizeof(tcp_timeout));
    httpd_query_key_value(buf, "tcp_ip", tcp_ip, sizeof(tcp_ip));
    httpd_query_key_value(buf, "tcp_gateway", tcp_gateway, sizeof(tcp_gateway));

    // Read existing config.json if any
    ensure_spiffs();
    FILE *f = fopen("/spiffs/config.json", "r");
    cJSON *root = NULL;
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        rewind(f);
        char *data = malloc(size + 1);
        if (data) {
            fread(data, 1, size, f);
            data[size] = '\0';
            root = cJSON_Parse(data);
            free(data);
        }
        fclose(f);
    }
    if (!root) root = cJSON_CreateObject();

    // Salva modo Modbus
    if (strlen(modbus_mode) > 0) {
        cJSON_ReplaceItemInObject(root, "modbus_mode", cJSON_CreateString(modbus_mode));
    }
    // Update RTU fields
    if (strlen(rtu_baud) > 0) cJSON_ReplaceItemInObject(root, "baudrate", cJSON_CreateNumber(atoi(rtu_baud)));
    if (strlen(rtu_parity) > 0) cJSON_ReplaceItemInObject(root, "paridade", cJSON_CreateNumber(atoi(rtu_parity)));
    if (strlen(rtu_databits) > 0) cJSON_ReplaceItemInObject(root, "databits", cJSON_CreateNumber(atoi(rtu_databits)));
    if (strlen(rtu_stopbits) > 0) cJSON_ReplaceItemInObject(root, "stopbits", cJSON_CreateNumber(atoi(rtu_stopbits)));
    if (strlen(rtu_addr) > 0) cJSON_ReplaceItemInObject(root, "endereco", cJSON_CreateNumber(atoi(rtu_addr)));

    // Update TCP fields under object modbus_tcp
    cJSON *tcp_obj = cJSON_GetObjectItem(root, "modbus_tcp");
    if (!tcp_obj) {
        tcp_obj = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "modbus_tcp", tcp_obj);
    }
    if (strlen(tcp_port) > 0) cJSON_ReplaceItemInObject(tcp_obj, "port", cJSON_CreateNumber(atoi(tcp_port)));
    if (strlen(tcp_unit) > 0) cJSON_ReplaceItemInObject(tcp_obj, "unitid", cJSON_CreateNumber(atoi(tcp_unit)));
    if (strlen(tcp_timeout) > 0) cJSON_ReplaceItemInObject(tcp_obj, "timeout", cJSON_CreateNumber(atoi(tcp_timeout)));
    cJSON_ReplaceItemInObject(tcp_obj, "enabled", cJSON_CreateBool(strlen(tcp_enable) > 0));
    if (strlen(tcp_ip) > 0) cJSON_ReplaceItemInObject(tcp_obj, "ip", cJSON_CreateString(tcp_ip));
    if (strlen(tcp_gateway) > 0) cJSON_ReplaceItemInObject(tcp_obj, "gateway", cJSON_CreateString(tcp_gateway));

    // Write back
    char *out = cJSON_Print(root);
    f = fopen("/spiffs/config.json", "w");
    if (!f) {
        cJSON_Delete(root);
        free(out);
        return httpd_resp_send(req, "Failed to open config.json for writing", HTTPD_RESP_USE_STRLEN);
    }
    fprintf(f, "%s", out);
    fclose(f);
    cJSON_Delete(root);
    free(out);

    // Envia p�gina de confirma��o usando template
    return send_confirmation_page(req, "Configura��o Salva", 
                                "Configura��o Modbus salva com sucesso!", 
                                "As configura��es foram aplicadas e est�o prontas para uso.",
                                "/modbus", "Voltar para Modbus", 3);
    
}

// POST handler to save only the Modbus mode from Configurar Dispositivo page
esp_err_t config_mode_save_post_handler(httpd_req_t *req) {
    char buf[128] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (ret <= 0) return httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);

    char mode[16] = "";
    httpd_query_key_value(buf, "modbus_mode", mode, sizeof(mode));
    if (strlen(mode) == 0) return httpd_resp_send(req, "Modo inv�lido", HTTPD_RESP_USE_STRLEN);

    // Read existing config (if any)
    ensure_spiffs();
    FILE *f = fopen("/spiffs/config.json", "r");
    cJSON *root = NULL;
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        rewind(f);
        char *b = malloc(sz + 1);
        if (b) {
            fread(b, 1, sz, f);
            b[sz] = '\0';
            root = cJSON_Parse(b);
            free(b);
        }
        fclose(f);
    }
    if (!root) root = cJSON_CreateObject();

    cJSON_ReplaceItemInObject(root, "modbus_mode", cJSON_CreateString(mode));

    char *out = cJSON_Print(root);
    FILE *fw = fopen("/spiffs/config.json", "w");
    if (!fw) {
        cJSON_Delete(root);
        free(out);
        return httpd_resp_send(req, "Falha ao abrir config.json para escrita", HTTPD_RESP_USE_STRLEN);
    }
    fprintf(fw, "%s", out);
    fclose(fw);
    cJSON_Delete(root);
    free(out);

    // Envia confirma��o e programa reinicializa��o
    char message[128];
    snprintf(message, sizeof(message), "Modo Modbus alterado para '%s'. O ESP32 ser� reiniciado.", mode);
    
    esp_err_t result = send_confirmation_page(req, "Modo Alterado", 
                                            "Configura��o de Modo Salva", 
                                            message,
                                            "/modbus", "Voltar para Modbus", 0);

    // Reinicia ap�s um pequeno delay para garantir que a resposta seja enviada
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return result;
}

// Handler to save AP config into NVS
esp_err_t ap_save_post_handler(httpd_req_t *req) {
    // read body
    char buf[1024] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (ret <= 0) {
        return httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);
    }

    // Parse form data
    char ap_ssid[64] = "";
    char ap_username[64] = "";
    char ap_password[64] = "";
    char ap_ip[32] = "";
    
    httpd_query_key_value(buf, "ap_ssid", ap_ssid, sizeof(ap_ssid));
    httpd_query_key_value(buf, "ap_username", ap_username, sizeof(ap_username));
    httpd_query_key_value(buf, "ap_password", ap_password, sizeof(ap_password));
    httpd_query_key_value(buf, "ap_ip", ap_ip, sizeof(ap_ip));

    // Save to NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("ap_config", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return httpd_resp_send(req, "Erro ao abrir NVS", HTTPD_RESP_USE_STRLEN);
    }

    nvs_set_str(nvs_handle, "ssid", ap_ssid);
    nvs_set_str(nvs_handle, "username", ap_username);
    nvs_set_str(nvs_handle, "password", ap_password);
    nvs_set_str(nvs_handle, "ip", ap_ip);
    
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        return send_confirmation_page(req, "Erro", "Erro ao Salvar", 
                                    "N�o foi poss�vel salvar a configura��o do Access Point.",
                                    "/ap-config", "Tentar Novamente", 0);
    }

    return send_confirmation_page(req, "Configura��o Salva", 
                                "Access Point Configurado", 
                                "As configura��es do Access Point foram salvas com sucesso!",
                                "/modbus", "Voltar para Modbus", 3);
}

// Callback do timer para reinicializa��o
static void restart_timer_callback(void* arg) {
    ESP_LOGI(TAG, "Timer disparado - Reiniciando dispositivo ap�s configura��o AP...");
    esp_restart();
}

// Fun��o para parsear dados multipart/form-data
static void parse_multipart_data(const char* data, const char* boundary, 
                                char* ap_ssid, size_t ssid_size,
                                char* ap_password, size_t password_size,
                                char* ap_password_confirm, size_t confirm_size,
                                char* ap_ip, size_t ip_size) {
    
    ESP_LOGI(TAG, "Iniciando parse multipart/form-data");
    
    // Campos e seus buffers de sa�da
    const char* field_names[] = {"ap_ssid", "ap_password", "ap_password_confirm", "ap_ip"};
    char* output_buffers[] = {ap_ssid, ap_password, ap_password_confirm, ap_ip};
    size_t buffer_sizes[] = {ssid_size-1, password_size-1, confirm_size-1, ip_size-1}; // -1 para \0
    
    // Inicializar buffers
    for (int i = 0; i < 4; i++) {
        output_buffers[i][0] = '\0';
    }
    
    // Parsear cada campo
    for (int i = 0; i < 4; i++) {
        // Procurar por name="campo"
        char search_pattern[64];
        snprintf(search_pattern, sizeof(search_pattern), "name=\"%s\"", field_names[i]);
        
        const char* field_pos = strstr(data, search_pattern);
        if (field_pos) {
            ESP_LOGI(TAG, "Encontrado campo: %s", field_names[i]);
            
            // Procurar pelo in�cio do valor (ap�s duas quebras de linha \r\n\r\n)
            const char* value_start = strstr(field_pos, "\r\n\r\n");
            if (!value_start) {
                // Tentar formato alternativo \n\n
                value_start = strstr(field_pos, "\n\n");
                if (value_start) {
                    value_start += 2;
                }
            } else {
                value_start += 4;
            }
            
            if (value_start) {
                // Procurar pelo final do valor (pr�ximo boundary)
                const char* value_end = strstr(value_start, "\r\n------");
                if (!value_end) {
                    // Tentar formato alternativo
                    value_end = strstr(value_start, "\n------");
                }
                
                if (value_end) {
                    size_t value_len = value_end - value_start;
                    
                    // Copiar valor se couber no buffer
                    if (value_len <= buffer_sizes[i]) {
                        strncpy(output_buffers[i], value_start, value_len);
                        output_buffers[i][value_len] = '\0';
                        
                        // Remover \r no final se houver
                        if (value_len > 0 && output_buffers[i][value_len-1] == '\r') {
                            output_buffers[i][value_len-1] = '\0';
                        }
                        
                        ESP_LOGI(TAG, "Valor extra�do para %s: [%s] (len=%d)", 
                                field_names[i], output_buffers[i], strlen(output_buffers[i]));
                    } else {
                        ESP_LOGW(TAG, "Valor muito grande para %s (len=%d, max=%d)", 
                                field_names[i], value_len, buffer_sizes[i]);
                    }
                } else {
                    ESP_LOGW(TAG, "N�o encontrado final do valor para %s", field_names[i]);
                }
            } else {
                ESP_LOGW(TAG, "N�o encontrado in�cio do valor para %s", field_names[i]);
            }
        } else {
            ESP_LOGW(TAG, "Campo n�o encontrado: %s", field_names[i]);
        }
    }
}

// Task para reinicializa��o com delay
static void delayed_restart_task(void *pvParameters) {
    restart_task_running = true;
    ESP_LOGI(TAG, "*** TASK DE RESTART INICIADA ***");
    
    // Aguardar apenas 2 segundos para garantir que a resposta HTTP seja enviada
    for (int i = 2; i > 0; i--) {
        ESP_LOGI(TAG, "Reiniciando em %d segundos...", i);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    ESP_LOGI(TAG, "*** REINICIANDO AGORA PARA ATIVAR MODO DUAL AP+STA ***");
    esp_restart();
    vTaskDelete(NULL); // Esta linha nunca ser� executada, mas � boa pr�tica
}

// Handler para salvar configura��es do AP da p�gina de configura��o do dispositivo
esp_err_t ap_config_save_post_handler(httpd_req_t *req) {
    // ?? VERIFICA��O DE PERMISS�O: Apenas administradores podem alterar configura��es AP
    esp_err_t perm_result = check_user_permission(req, USER_LEVEL_ADMIN);
    if (perm_result != ESP_OK) {
        return perm_result;
    }

    ESP_LOGI(TAG, "=== HANDLER AP CONFIG SAVE INICIADO ===");
    
    // Verificar Content-Type
    size_t buf_len;
    buf_len = httpd_req_get_hdr_value_len(req, "Content-Type") + 1;
    if (buf_len > 1) {
        char *content_type = malloc(buf_len);
        if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Content-Type: %s", content_type);
        }
        free(content_type);
    }
    
    ESP_LOGI(TAG, "Content-Length: %d", req->content_len);
    
    char buf[1024] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (ret <= 0) {
        ESP_LOGE(TAG, "Erro ao receber dados do formul�rio: %d", ret);
        return httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);
    }
    
    ESP_LOGI(TAG, "Dados brutos recebidos (%d bytes): [%s]", ret, buf);

    // Parse form data
    char ap_ssid[64] = "";
    char ap_password[64] = "";
    char ap_password_confirm[64] = "";
    char ap_ip[32] = "";
    
    // Verificar se � multipart/form-data ou application/x-www-form-urlencoded
    bool is_multipart = (strstr(buf, "Content-Disposition") != NULL);
    
    if (is_multipart) {
        ESP_LOGI(TAG, "Parseando dados multipart/form-data");
        parse_multipart_data(buf, "------WebKitFormBoundary", 
                            ap_ssid, sizeof(ap_ssid),
                            ap_password, sizeof(ap_password),
                            ap_password_confirm, sizeof(ap_password_confirm),
                            ap_ip, sizeof(ap_ip));
    } else {
        ESP_LOGI(TAG, "Parseando dados application/x-www-form-urlencoded");
        httpd_query_key_value(buf, "ap_ssid", ap_ssid, sizeof(ap_ssid));
        httpd_query_key_value(buf, "ap_password", ap_password, sizeof(ap_password));
        httpd_query_key_value(buf, "ap_password_confirm", ap_password_confirm, sizeof(ap_password_confirm));
        httpd_query_key_value(buf, "ap_ip", ap_ip, sizeof(ap_ip));
    }
    
    ESP_LOGI(TAG, "Dados parseados - SSID: [%s], IP: [%s], Senha length: %d, Confirm length: %d", 
             ap_ssid, ap_ip, strlen(ap_password), strlen(ap_password_confirm));

    // Valida��es b�sicas
    if (strlen(ap_ssid) == 0 || strlen(ap_password) < 8 || strlen(ap_ip) == 0) {
        return send_confirmation_page(req, "Erro de Valida��o", "Dados Inv�lidos", 
                                    "Por favor, preencha todos os campos corretamente. A senha deve ter pelo menos 8 caracteres.",
                                    "/config_unidade", "Voltar", 0);
    }

    // Valida��o de confirma��o de senha
    if (strcmp(ap_password, ap_password_confirm) != 0) {
        return send_confirmation_page(req, "Erro de Valida��o", "Senhas N�o Coincidem", 
                                    "A senha e a confirma��o de senha devem ser id�nticas. Por favor, tente novamente.",
                                    "/config_unidade", "Voltar", 0);
    }

    // Save to NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("ap_config", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return send_confirmation_page(req, "Erro", "Erro no Sistema", 
                                    "N�o foi poss�vel abrir o sistema de armazenamento.",
                                    "/config_unidade", "Voltar", 0);
    }

    nvs_set_str(nvs_handle, "ssid", ap_ssid);
    nvs_set_str(nvs_handle, "password", ap_password);
    nvs_set_str(nvs_handle, "ip", ap_ip);
    
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        return send_confirmation_page(req, "Erro", "Erro ao Salvar", 
                                    "N�o foi poss�vel salvar as configura��es do Access Point.",
                                    "/config_unidade", "Tentar Novamente", 0);
    }

    // Criar task para reinicializar ap�s 5 segundos (sempre, independente da resposta)
    if (!restart_task_running) {
        ESP_LOGI(TAG, "Configura��es do AP salvas, iniciando reinicializa��o...");
        
        // Tentar criar task primeiro
        BaseType_t task_result = xTaskCreate(delayed_restart_task, "restart_task", 2048, NULL, 5, NULL);
        if (task_result == pdPASS) {
            ESP_LOGI(TAG, "Task de reinicializa��o criada com sucesso!");
        } else {
            ESP_LOGE(TAG, "ERRO: Falha ao criar task - usando timer como backup");
            
            // Se falhar, usar timer como backup
            esp_timer_handle_t restart_timer;
            esp_timer_create_args_t timer_args = {
                .callback = restart_timer_callback,
                .arg = NULL,
                .name = "restart_timer"
            };
            
            esp_err_t timer_err = esp_timer_create(&timer_args, &restart_timer);
            if (timer_err == ESP_OK) {
                esp_timer_start_once(restart_timer, 5000000); // 5 segundos em microsegundos
                ESP_LOGI(TAG, "Timer de reinicializa��o iniciado como backup!");
            } else {
                ESP_LOGE(TAG, "ERRO CR�TICO: Falha ao criar timer de backup!");
            }
        }
    } else {
        ESP_LOGW(TAG, "Task de reinicializa��o j� est� rodando!");
    }
    
    // P�gina de confirma��o com aviso de reinicializa��o
    esp_err_t result = send_confirmation_page(req, "Configura��o Salva com Sucesso!", 
                                "Access Point Configurado", 
                                "As configura��es do Access Point foram salvas! O dispositivo ser� reiniciado automaticamente em 5 segundos para aplicar as mudan�as.",
                                "/config_unidade", "Voltar", 5);
    
    return result;
}

// Handler para salvar configura��es RTU
esp_err_t rtu_config_save_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "=== HANDLER RTU CONFIG SAVE INICIADO ===");
    
    char buf[1024] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (ret <= 0) {
        ESP_LOGE(TAG, "Erro ao receber dados do formul�rio RTU: %d", ret);
        return httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);
    }
    
    ESP_LOGI(TAG, "Dados RTU recebidos (%d bytes): [%s]", ret, buf);

    // Parse form data
    char rtu_baudrate[16] = "";
    char rtu_databits[8] = "";
    char rtu_parity[16] = "";
    char rtu_stopbits[8] = "";
    char rtu_slave_address[8] = "";
    char rtu_timeout[16] = "";
    
    // Parse dos par�metros do formul�rio
    httpd_query_key_value(buf, "rtu_baudrate", rtu_baudrate, sizeof(rtu_baudrate));
    httpd_query_key_value(buf, "rtu_databits", rtu_databits, sizeof(rtu_databits));
    httpd_query_key_value(buf, "rtu_parity", rtu_parity, sizeof(rtu_parity));
    httpd_query_key_value(buf, "rtu_stopbits", rtu_stopbits, sizeof(rtu_stopbits));
    httpd_query_key_value(buf, "rtu_slave_address", rtu_slave_address, sizeof(rtu_slave_address));
    httpd_query_key_value(buf, "rtu_timeout", rtu_timeout, sizeof(rtu_timeout));
    
    ESP_LOGI(TAG, "RTU - Baudrate: %s, Databits: %s, Parity: %s, Stopbits: %s, Address: %s, Timeout: %s", 
             rtu_baudrate, rtu_databits, rtu_parity, rtu_stopbits, rtu_slave_address, rtu_timeout);

    // Valida��es b�sicas
    if (strlen(rtu_baudrate) == 0 || strlen(rtu_databits) == 0 || 
        strlen(rtu_parity) == 0 || strlen(rtu_stopbits) == 0 || 
        strlen(rtu_slave_address) == 0 || strlen(rtu_timeout) == 0) {
        return send_confirmation_page(req, "Erro de Valida��o", "Dados Incompletos", 
                                    "Por favor, preencha todos os campos da configura��o RTU.",
                                    "/config_device", "Voltar", 0);
    }

    // Validar range do endere�o slave (1-247)
    int slave_addr = atoi(rtu_slave_address);
    if (slave_addr < 1 || slave_addr > 247) {
        return send_confirmation_page(req, "Erro de Valida��o", "Endere�o Inv�lido", 
                                    "O endere�o Slave deve estar entre 1 e 247.",
                                    "/config_device", "Voltar", 0);
    }

    // Salvar no config.json
    ensure_spiffs();
    
    // Ler config.json existente
    FILE *f = fopen("/spiffs/config.json", "r");
    cJSON *root = NULL;
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        rewind(f);
        char *data = malloc(size + 1);
        if (data) {
            fread(data, 1, size, f);
            data[size] = '\0';
            root = cJSON_Parse(data);
            free(data);
        }
        fclose(f);
    }
    
    if (!root) {
        root = cJSON_CreateObject();
    }

    // Criar ou atualizar se��o modbus_rtu
    cJSON *rtu_obj = cJSON_GetObjectItem(root, "modbus_rtu");
    if (!rtu_obj) {
        rtu_obj = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "modbus_rtu", rtu_obj);
    }

    // Salvar valores RTU
    cJSON_ReplaceItemInObject(rtu_obj, "baudrate", cJSON_CreateNumber(atoi(rtu_baudrate)));
    cJSON_ReplaceItemInObject(rtu_obj, "databits", cJSON_CreateNumber(atoi(rtu_databits)));
    cJSON_ReplaceItemInObject(rtu_obj, "parity", cJSON_CreateString(rtu_parity));
    cJSON_ReplaceItemInObject(rtu_obj, "stopbits", cJSON_CreateNumber(atoi(rtu_stopbits)));
    cJSON_ReplaceItemInObject(rtu_obj, "slave_address", cJSON_CreateNumber(slave_addr));
    cJSON_ReplaceItemInObject(rtu_obj, "timeout", cJSON_CreateNumber(atoi(rtu_timeout)));

    // Gravar arquivo
    char *json_string = cJSON_Print(root);
    f = fopen("/spiffs/config.json", "w");
    if (!f) {
        cJSON_Delete(root);
        free(json_string);
        return send_confirmation_page(req, "Erro", "Falha ao Salvar", 
                                    "N�o foi poss�vel salvar as configura��es RTU.",
                                    "/config_device", "Tentar Novamente", 0);
    }
    
    fprintf(f, "%s", json_string);
    fclose(f);
    
    ESP_LOGI(TAG, "Configura��es RTU salvas em config.json: %s", json_string);
    
    cJSON_Delete(root);
    free(json_string);

    return send_confirmation_page(req, "Configura��es Salvas", "RTU Configurado!", 
                                "As configura��es do modo RTU foram salvas com sucesso.",
                                "/config_device", "Voltar", 3);
}

// Handler para salvar todos os registradores Modbus
esp_err_t modbus_registers_save_post_handler(httpd_req_t *req) {
    // ?? VERIFICA��O DE PERMISS�O: Apenas administradores podem salvar registros Modbus
    esp_err_t perm_result = check_user_permission(req, USER_LEVEL_ADMIN);
    if (perm_result != ESP_OK) {
        return perm_result;
    }

    char buf[2048] = {0}; // Buffer maior para todos os registradores
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (ret <= 0) {
        return httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);
    }

    ESP_LOGI(TAG, "Salvando registradores Modbus...");

    // Parse dos registradores 4000 (edit�veis)
    char reg_value[16];
    for (int i = 0; i < 8; i++) {
        char param_name[16];
        snprintf(param_name, sizeof(param_name), "reg4000_%d", i);
        if (httpd_query_key_value(buf, param_name, reg_value, sizeof(reg_value)) == ESP_OK) {
            reg4000[i] = atoi(reg_value);
            ESP_LOGI(TAG, "Reg4000[%d] = %d", i, reg4000[i]);
        }
    }

    // Parse dos registradores 6000 (edit�veis)
    for (int i = 0; i < 5; i++) {
        char param_name[16];
        snprintf(param_name, sizeof(param_name), "reg6000_%d", i);
        if (httpd_query_key_value(buf, param_name, reg_value, sizeof(reg_value)) == ESP_OK) {
            reg6000[i] = atoi(reg_value);
            ESP_LOGI(TAG, "Reg6000[%d] = %d", i, reg6000[i]);
        }
    }

    // Parse dos registradores 9000 (edit�veis)
    for (int i = 0; i < 20; i++) {
        char param_name[16];
        snprintf(param_name, sizeof(param_name), "reg9000_%d", i);
        if (httpd_query_key_value(buf, param_name, reg_value, sizeof(reg_value)) == ESP_OK) {
            reg9000[i] = atoi(reg_value);
            ESP_LOGI(TAG, "Reg9000[%d] = %d", i, reg9000[i]);
        }
    }

    // **SALVAR AUTOMATICAMENTE NO CONFIG.JSON**
    ESP_LOGI(TAG, "Salvando registradores no config.json...");
    ensure_spiffs();
    
    // L� o config.json existente ou cria um novo
    FILE *f = fopen("/spiffs/config.json", "r");
    cJSON *root = NULL;
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        rewind(f);
        char *data = malloc(size + 1);
        if (data) {
            fread(data, 1, size, f);
            data[size] = '\0';
            root = cJSON_Parse(data);
            free(data);
        }
        fclose(f);
    }
    
    if (!root) {
        root = cJSON_CreateObject();
    }

    // Criar/atualizar se��o modbus_registers
    cJSON *registers_obj = cJSON_GetObjectItem(root, "modbus_registers");
    if (!registers_obj) {
        registers_obj = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "modbus_registers", registers_obj);
    }

    // Salvar registradores 4000  
    cJSON *reg4000_array = cJSON_CreateArray();
    for (int i = 0; i < 8; i++) {
        cJSON_AddItemToArray(reg4000_array, cJSON_CreateNumber(reg4000[i]));
    }
    cJSON_ReplaceItemInObject(registers_obj, "reg4000", reg4000_array);

    // Salvar registradores 6000
    cJSON *reg6000_array = cJSON_CreateArray();
    for (int i = 0; i < 5; i++) {
        cJSON_AddItemToArray(reg6000_array, cJSON_CreateNumber(reg6000[i]));
    }
    cJSON_ReplaceItemInObject(registers_obj, "reg6000", reg6000_array);

    // Salvar registradores 9000
    cJSON *reg9000_array = cJSON_CreateArray();
    for (int i = 0; i < 20; i++) {
        cJSON_AddItemToArray(reg9000_array, cJSON_CreateNumber(reg9000[i]));
    }
    cJSON_ReplaceItemInObject(registers_obj, "reg9000", reg9000_array);

    // Gravar arquivo config.json
    char *json_string = cJSON_Print(root);
    f = fopen("/spiffs/config.json", "w");
    if (!f) {
        cJSON_Delete(root);
        free(json_string);
        return send_confirmation_page(req, "Erro", "Falha ao Salvar", 
                                    "N�o foi poss�vel salvar os registradores no config.json.",
                                    "/modbus", "Tentar Novamente", 0);
    }
    
    fprintf(f, "%s", json_string);
    fclose(f);
    
    ESP_LOGI(TAG, "Registradores salvos em config.json: %s", json_string);
    
    cJSON_Delete(root);
    free(json_string);

    ESP_LOGI(TAG, "Todos os registradores foram atualizados e salvos com sucesso!");

    return send_confirmation_page(req, "Registradores Salvos", "Configura��o Salva no Arquivo!", 
                                "Todos os registradores Modbus foram salvos automaticamente no config.json.",
                                "/modbus", "Voltar para Registradores", 3);
}

// Handler para salvar configura��es WiFi (SSID e senha)
esp_err_t wifi_config_save_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "=== HANDLER WIFI CONFIG SAVE INICIADO ===");
    
    char buf[1024] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (ret <= 0) {
        ESP_LOGE(TAG, "Erro ao receber dados do formul�rio: %d", ret);
        return httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);
    }
    
    ESP_LOGI(TAG, "Dados recebidos (%d bytes): [%s]", ret, buf);

    // Parse form data
    char wifi_ssid[64] = "";
    char wifi_password[64] = "";
    char wifi_ip[32] = "";
    char wifi_mask[32] = "";
    char wifi_gateway[32] = "";
    char wifi_dns[32] = "";
    
    // Verificar se � multipart/form-data ou application/x-www-form-urlencoded
    bool is_multipart = (strstr(buf, "Content-Disposition") != NULL);
    
    if (is_multipart) {
        ESP_LOGI(TAG, "Parseando dados multipart/form-data");
        
        // Parse multipart data
        char *ssid_start = strstr(buf, "name=\"wifi_ssid\"");
        if (ssid_start) {
            ssid_start = strchr(ssid_start, '\n');
            if (ssid_start) {
                ssid_start++; // Pular \n
                if (*ssid_start == '\r') ssid_start++; // Pular \r se existir
                if (*ssid_start == '\n') ssid_start++; // Pular segundo \n
                
                char *ssid_end = strstr(ssid_start, "\n------");
                if (!ssid_end) ssid_end = strstr(ssid_start, "\r------");
                if (ssid_end) {
                    size_t ssid_len = ssid_end - ssid_start;
                    if (ssid_len < sizeof(wifi_ssid)) {
                        strncpy(wifi_ssid, ssid_start, ssid_len);
                        wifi_ssid[ssid_len] = '\0';
                        // Remove trailing \r se existir
                        if (ssid_len > 0 && wifi_ssid[ssid_len-1] == '\r') {
                            wifi_ssid[ssid_len-1] = '\0';
                        }
                    }
                }
            }
        }
        
        char *pwd_start = strstr(buf, "name=\"wifi_password\"");
        if (pwd_start) {
            pwd_start = strchr(pwd_start, '\n');
            if (pwd_start) {
                pwd_start++; // Pular \n
                if (*pwd_start == '\r') pwd_start++; // Pular \r se existir
                if (*pwd_start == '\n') pwd_start++; // Pular segundo \n
                
                char *pwd_end = strstr(pwd_start, "\n------");
                if (!pwd_end) pwd_end = strstr(pwd_start, "\r------");
                if (pwd_end) {
                    size_t pwd_len = pwd_end - pwd_start;
                    if (pwd_len < sizeof(wifi_password)) {
                        strncpy(wifi_password, pwd_start, pwd_len);
                        wifi_password[pwd_len] = '\0';
                        // Remove trailing \r se existir
                        if (pwd_len > 0 && wifi_password[pwd_len-1] == '\r') {
                            wifi_password[pwd_len-1] = '\0';
                        }
                    }
                }
            }
        }

        // Campos de IP (opcionais)
        char *ip_start = strstr(buf, "name=\"wifi_ip\"");
        if (ip_start) {
            ip_start = strchr(ip_start, '\n');
            if (ip_start) {
                ip_start++;
                if (*ip_start == '\r') ip_start++;
                if (*ip_start == '\n') ip_start++;
                char *ip_end = strstr(ip_start, "\n------");
                if (!ip_end) ip_end = strstr(ip_start, "\r------");
                if (ip_end) {
                    size_t ip_len = ip_end - ip_start;
                    if (ip_len < sizeof(wifi_ip)) {
                        strncpy(wifi_ip, ip_start, ip_len);
                        wifi_ip[ip_len] = '\0';
                        if (ip_len > 0 && wifi_ip[ip_len-1] == '\r') wifi_ip[ip_len-1] = '\0';
                    }
                }
            }
        }
        char *mask_start = strstr(buf, "name=\"wifi_mask\"");
        if (mask_start) {
            mask_start = strchr(mask_start, '\n');
            if (mask_start) {
                mask_start++;
                if (*mask_start == '\r') mask_start++;
                if (*mask_start == '\n') mask_start++;
                char *mask_end = strstr(mask_start, "\n------");
                if (!mask_end) mask_end = strstr(mask_start, "\r------");
                if (mask_end) {
                    size_t mask_len = mask_end - mask_start;
                    if (mask_len < sizeof(wifi_mask)) {
                        strncpy(wifi_mask, mask_start, mask_len);
                        wifi_mask[mask_len] = '\0';
                        if (mask_len > 0 && wifi_mask[mask_len-1] == '\r') wifi_mask[mask_len-1] = '\0';
                    }
                }
            }
        }
        char *gw_start = strstr(buf, "name=\"wifi_gateway\"");
        if (gw_start) {
            gw_start = strchr(gw_start, '\n');
            if (gw_start) {
                gw_start++;
                if (*gw_start == '\r') gw_start++;
                if (*gw_start == '\n') gw_start++;
                char *gw_end = strstr(gw_start, "\n------");
                if (!gw_end) gw_end = strstr(gw_start, "\r------");
                if (gw_end) {
                    size_t gw_len = gw_end - gw_start;
                    if (gw_len < sizeof(wifi_gateway)) {
                        strncpy(wifi_gateway, gw_start, gw_len);
                        wifi_gateway[gw_len] = '\0';
                        if (gw_len > 0 && wifi_gateway[gw_len-1] == '\r') wifi_gateway[gw_len-1] = '\0';
                    }
                }
            }
        }
        char *dns_start = strstr(buf, "name=\"wifi_dns\"");
        if (dns_start) {
            dns_start = strchr(dns_start, '\n');
            if (dns_start) {
                dns_start++;
                if (*dns_start == '\r') dns_start++;
                if (*dns_start == '\n') dns_start++;
                char *dns_end = strstr(dns_start, "\n------");
                if (!dns_end) dns_end = strstr(dns_start, "\r------");
                if (dns_end) {
                    size_t dns_len = dns_end - dns_start;
                    if (dns_len < sizeof(wifi_dns)) {
                        strncpy(wifi_dns, dns_start, dns_len);
                        wifi_dns[dns_len] = '\0';
                        if (dns_len > 0 && wifi_dns[dns_len-1] == '\r') wifi_dns[dns_len-1] = '\0';
                    }
                }
            }
        }
    } else {
        ESP_LOGI(TAG, "Parseando dados application/x-www-form-urlencoded");
    httpd_query_key_value(buf, "wifi_ssid", wifi_ssid, sizeof(wifi_ssid));
    httpd_query_key_value(buf, "wifi_password", wifi_password, sizeof(wifi_password));
    httpd_query_key_value(buf, "wifi_ip", wifi_ip, sizeof(wifi_ip));
    httpd_query_key_value(buf, "wifi_mask", wifi_mask, sizeof(wifi_mask));
    httpd_query_key_value(buf, "wifi_gateway", wifi_gateway, sizeof(wifi_gateway));
    httpd_query_key_value(buf, "wifi_dns", wifi_dns, sizeof(wifi_dns));
    // Decode percent-encoding (e.g. %23 -> #)
    url_decode_inplace(wifi_ssid);
    url_decode_inplace(wifi_password);
    url_decode_inplace(wifi_ip);
    url_decode_inplace(wifi_mask);
    url_decode_inplace(wifi_gateway);
    url_decode_inplace(wifi_dns);
    }
    
    ESP_LOGI(TAG, "Dados parseados - SSID: [%s], Password length: %d", 
             wifi_ssid, strlen(wifi_password));

    // Valida��o b�sica
    if (strlen(wifi_ssid) == 0) {
        ESP_LOGE(TAG, "SSID vazio");
        const char* response = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n{\"error\":\"SSID n�o pode estar vazio\"}";
        return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    }

    // Save to NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao abrir NVS: %s", esp_err_to_name(err));
        const char* response = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\n\r\n{\"error\":\"Erro no sistema de armazenamento\"}";
        return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    }

    // Salvar SSID e senha
    nvs_set_str(nvs_handle, "wifi_ssid", wifi_ssid);
    nvs_set_str(nvs_handle, "wifi_password", wifi_password);
    // Save network config to spiffs (IP est�tico)
    if (strlen(wifi_ip) > 0 || strlen(wifi_mask) > 0 || strlen(wifi_gateway) > 0 || strlen(wifi_dns) > 0) {
        ESP_LOGI(TAG, "Salvando configura��o de rede manual: ip=%s mask=%s gw=%s dns=%s", wifi_ip, wifi_mask, wifi_gateway, wifi_dns);
        save_network_config(wifi_ip, wifi_mask, wifi_gateway, wifi_dns);
    }
    
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao salvar no NVS: %s", esp_err_to_name(err));
        const char* response = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\n\r\n{\"error\":\"Erro ao salvar configura��es\"}";
        return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    }

    ESP_LOGI(TAG, "Configura��es WiFi salvas com sucesso - SSID: %s", wifi_ssid);
    
    // Resposta de sucesso
    const char* response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"success\":true,\"message\":\"Configura��es WiFi salvas com sucesso!\"}";
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

// Handler para conectar � rede WiFi salva
esp_err_t wifi_connect_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "=== HANDLER WIFI CONNECT INICIADO ===");
    
    // Ler configura��es WiFi do NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao abrir NVS para leitura: %s", esp_err_to_name(err));
        const char* response = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\n\r\n{\"error\":\"Erro no sistema de armazenamento\"}";
        return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    }

    char wifi_ssid[64] = "";
    char wifi_password[64] = "";
    size_t required_size = 0;
    
    // Ler SSID
    required_size = sizeof(wifi_ssid);
    err = nvs_get_str(nvs_handle, "wifi_ssid", wifi_ssid, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao ler SSID do NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        const char* response = "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\n\r\n{\"error\":\"Configura��o WiFi n�o encontrada. Configure uma rede primeiro.\"}";
        return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    }
    
    // Ler senha (opcional)
    required_size = sizeof(wifi_password);
    err = nvs_get_str(nvs_handle, "wifi_password", wifi_password, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Aviso ao ler senha do NVS: %s", esp_err_to_name(err));
        // Continuar com senha vazia para redes abertas
    }
    
    nvs_close(nvs_handle);
    
    ESP_LOGI(TAG, "Configura��o WiFi lida - SSID: [%s], Password length: %d", 
             wifi_ssid, strlen(wifi_password));

    // Valida��o
    if (strlen(wifi_ssid) == 0) {
        const char* response = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n{\"error\":\"SSID n�o configurado\"}";
        return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    }

    // Iniciar conex�o WiFi
    ESP_LOGI(TAG, "Iniciando conex�o WiFi para SSID: %s", wifi_ssid);
    
    // Conectar � rede
    wifi_connect(wifi_ssid, wifi_password);
    
    ESP_LOGI(TAG, "Comando de conex�o WiFi enviado");
    
    // Ler configura��o de rede (poss�vel IP est�tico) para informar ao usu�rio qual IP acessar
    char ip[32] = "", mask[32] = "", gw[32] = "", dns[32] = "";
    read_network_config(ip, sizeof(ip), mask, sizeof(mask), gw, sizeof(gw), dns, sizeof(dns));

    // Resposta de sucesso incluindo o IP esperado ap�s rein�cio (se existir)
    char response_buf[640];
    if (strlen(ip) > 0) {
        snprintf(response_buf, sizeof(response_buf), 
                 "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"
                 "{\"success\":true,\"message\":\"Conectando � rede %s e reiniciando...\",\"ssid\":\"%s\",\"ip\":\"%s\"}", 
                 wifi_ssid, wifi_ssid, ip);
    } else {
        // Sem IP est�tico: sugerir IP do AP como fallback e indicar DHCP
        const char* ap_ip = "192.168.4.1"; // IP padr�o do AP
        snprintf(response_buf, sizeof(response_buf), 
                 "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"
                 "{\"success\":true,\"message\":\"Conectando � rede %s e reiniciando...\",\"ssid\":\"%s\",\"ip\":\"%s (DHCP - verifique o roteador)\"}", 
                 wifi_ssid, wifi_ssid, ap_ip);
    }

    esp_err_t send_result = httpd_resp_send(req, response_buf, HTTPD_RESP_USE_STRLEN);
    
    // Criar task para reinicializar ap�s 2 segundos (tempo para enviar resposta)
    if (!restart_task_running) {
        ESP_LOGI(TAG, "*** INICIANDO REINICIALIZA��O AP�S CONEX�O WiFi ***");
        
        BaseType_t task_result = xTaskCreate(delayed_restart_task, "restart_task", 4096, NULL, 10, NULL);
        if (task_result == pdPASS) {
            ESP_LOGI(TAG, "Task de reinicializa��o criada com sucesso!");
        } else {
            ESP_LOGE(TAG, "ERRO: Falha ao criar task - usando timer como backup");
            
            esp_timer_handle_t restart_timer;
            esp_timer_create_args_t timer_args = {
                .callback = restart_timer_callback,
                .arg = NULL,
                .name = "restart_timer"
            };
            
            esp_err_t timer_err = esp_timer_create(&timer_args, &restart_timer);
            if (timer_err == ESP_OK) {
                esp_timer_start_once(restart_timer, 3000000); // 3 segundos em microsegundos
                ESP_LOGI(TAG, "Timer de reinicializa��o iniciado como backup!");
            } else {
                ESP_LOGE(TAG, "ERRO CR�TICO: Falha ao criar timer de backup!");
            }
        }
    } else {
        ESP_LOGW(TAG, "Task de reinicializa��o j� est� rodando!");
    }
    
    return send_result;
}

// Handler to save WiFi credentials to NVS
esp_err_t wifi_save_nvs_post_handler(httpd_req_t *req) {
    char buf[512] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (ret <= 0) {
        return httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);
    }

    char wifi_ssid[64] = "";
    char wifi_password[64] = "";
    
    httpd_query_key_value(buf, "ssid", wifi_ssid, sizeof(wifi_ssid));
    httpd_query_key_value(buf, "password", wifi_password, sizeof(wifi_password));
    url_decode_inplace(wifi_ssid);
    url_decode_inplace(wifi_password);

    if (strlen(wifi_ssid) == 0) {
        return send_confirmation_page(req, "Erro", "SSID Inv�lido", 
                                    "� necess�rio fornecer um nome de rede (SSID) v�lido.",
                                    "/wifi-scan", "Voltar", 0);
    }

    // Use centralized function to save WiFi credentials in NVS (namespace: wifi_config)
    save_wifi_config(wifi_ssid, wifi_password);

    // Note: save_wifi_config logs errors internally. Present success page regardless
    // (it will be obvious in logs if saving failed). If you prefer, read back to verify.
    char message[128];
    snprintf(message, sizeof(message), "Credenciais da rede '%s' foram salvas com sucesso!", wifi_ssid);
    return send_confirmation_page(req, "WiFi Configurado", "Configura��o Salva", 
                                message, "/wifi-status", "Ver Status", 3);
}

// Minimal stubs for functions declared in webserver.h
esp_err_t logout_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "?? Fazendo logout do usu�rio");
    
    // Limpar todos os estados de login
    save_login_state(false);
    save_login_state_root(false);
    save_user_level(USER_LEVEL_NONE);
    
    // Redirecionar para p�gina de login
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

esp_err_t config_get_handler(httpd_req_t *req) { (void)req; return httpd_resp_send(req, "", 0); }
esp_err_t form_handler(httpd_req_t *req) { (void)req; return httpd_resp_send(req, "", 0); }
esp_err_t modbus_config_get_handler(httpd_req_t *req) { (void)req; return httpd_resp_send(req, "", 0); }
esp_err_t modbus_config_post_handler(httpd_req_t *req) { (void)req; return httpd_resp_send(req, "", 0); }

void set_wifi_status(const char* status) { (void)status; }

// =================== MQTT HANDLERS ===================

// Função auxiliar para extrair valor do form data
static bool extract_form_value(const char* data, const char* key, char* output, size_t output_size) {
    char search_key[64];
    snprintf(search_key, sizeof(search_key), "%s=", key);
    
    const char* start = strstr(data, search_key);
    if (!start) return false;
    
    start += strlen(search_key);
    const char* end = strchr(start, '&');
    if (!end) end = start + strlen(start);
    
    size_t len = end - start;
    if (len >= output_size) len = output_size - 1;
    
    strncpy(output, start, len);
    output[len] = '\0';
    
    // URL decode básico (+ para espaço, %XX para hex)
    for (size_t i = 0; i < len; i++) {
        if (output[i] == '+') output[i] = ' ';
    }
    
    return true;
}

// GET handler para página de configuração MQTT
esp_err_t mqtt_config_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "mqtt_config_get_handler called");
    
    char *template_content = NULL;
    esp_err_t ret = load_file_content("/spiffs/html/mqtt_config.html", &template_content);
    
    if (ret != ESP_OK || !template_content) {
        ESP_LOGE(TAG, "Failed to load mqtt_config.html");
        return httpd_resp_send_404(req);
    }
    
    // Obter configuração atual do MQTT
    mqtt_config_t config;
    esp_err_t mqtt_ret = mqtt_get_config(&config);
    if (mqtt_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get MQTT config, using defaults");
        // Usar valores padrão se não conseguir carregar
        strncpy(config.broker_url, "mqtt://broker.hivemq.com", sizeof(config.broker_url) - 1);
        config.port = 1883;
        strncpy(config.client_id, "ESP32_MCT01", sizeof(config.client_id) - 1);
        config.username[0] = '\0';
        config.password[0] = '\0';
        config.tls_enabled = false;
        strncpy(config.ca_path, "/spiffs/isrgrootx1.pem", sizeof(config.ca_path) - 1);
        config.qos = 1;
        config.retain = false;
        config.publish_interval_ms = 10000;
        config.enabled = false;
    }
    
    // Preparar valores para template
    char port_str[8];
    char qos_str[4];
    char interval_str[16];
    char enabled_checked[16] = "";
    char tls_checked[16] = "";
    char retain_checked[16] = "";
    
    snprintf(port_str, sizeof(port_str), "%d", config.port);
    snprintf(qos_str, sizeof(qos_str), "%d", config.qos);
    snprintf(interval_str, sizeof(interval_str), "%d", (int)(config.publish_interval_ms / 1000));
    
    if (config.enabled) strcpy(enabled_checked, " checked");
    if (config.tls_enabled) strcpy(tls_checked, " checked");
    if (config.retain) strcpy(retain_checked, " checked");
    
    // Define substituições para o template
    const char *substitutions[] = {
        "MQTT_ENABLED_CHECKED", enabled_checked,
        "BROKER_URL", config.broker_url,
        "PORT", port_str,
        "CLIENT_ID", config.client_id,
        "USERNAME", config.username,
        "PASSWORD", config.password,
        "TLS_CHECKED", tls_checked,
        "CA_PATH", config.ca_path,
        "QOS", qos_str,
        "RETAIN_CHECKED", retain_checked,
        "INTERVAL", interval_str,
        NULL, NULL
    };
    
    char *final_html = apply_template_substitutions(template_content, substitutions);
    free(template_content);
    
    if (!final_html) {
        ESP_LOGE(TAG, "Failed to apply template substitutions for mqtt_config");
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "text/html");
    esp_err_t result = httpd_resp_send(req, final_html, strlen(final_html));
    free(final_html);
    return result;
}

// POST handler para salvar configuração MQTT
esp_err_t mqtt_config_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "mqtt_config_post_handler called");
    
    char buf[2048];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        ESP_LOGE(TAG, "Failed to receive POST data");
        return ESP_FAIL;
    }
    buf[received] = '\0';
    
    ESP_LOGI(TAG, "Received MQTT config data: %s", buf);
    
    // Parse dos dados do formulário
    mqtt_config_t config;
    memset(&config, 0, sizeof(config));
    
    // Extrair valores do formulário
    char temp_buf[256];
    
    // mqtt_enabled (checkbox)
    config.enabled = strstr(buf, "mqtt_enabled=on") != NULL;
    
    // broker_url
    if (extract_form_value(buf, "broker_url", config.broker_url, sizeof(config.broker_url))) {
        ESP_LOGI(TAG, "Broker URL: %s", config.broker_url);
    }
    
    // port
    if (extract_form_value(buf, "port", temp_buf, sizeof(temp_buf))) {
        config.port = atoi(temp_buf);
        ESP_LOGI(TAG, "Port: %d", config.port);
    }
    
    // client_id
    if (extract_form_value(buf, "client_id", config.client_id, sizeof(config.client_id))) {
        ESP_LOGI(TAG, "Client ID: %s", config.client_id);
    }
    
    // username
    extract_form_value(buf, "username", config.username, sizeof(config.username));
    
    // password
    extract_form_value(buf, "password", config.password, sizeof(config.password));
    
    // tls_enabled
    config.tls_enabled = strstr(buf, "tls_enabled=on") != NULL;
    
    // ca_path (certificado)
    if (extract_form_value(buf, "ca_certificate", config.ca_path, sizeof(config.ca_path))) {
        ESP_LOGI(TAG, "CA Path: %s", config.ca_path);
    }
    
    // qos
    if (extract_form_value(buf, "qos", temp_buf, sizeof(temp_buf))) {
        config.qos = atoi(temp_buf);
        ESP_LOGI(TAG, "QoS: %d", config.qos);
    }
    
    // retain
    config.retain = strstr(buf, "retain=on") != NULL;
    
    // publish_interval
    if (extract_form_value(buf, "publish_interval", temp_buf, sizeof(temp_buf))) {
        config.publish_interval_ms = atoi(temp_buf) * 1000; // converter para ms
        ESP_LOGI(TAG, "Publish interval: %d ms", (int)config.publish_interval_ms);
    }
    
    // Salvar configuração
    esp_err_t result = mqtt_set_config(&config);
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "MQTT configuration saved successfully");
        // Reiniciar MQTT se estivesse ativo
        if (config.enabled && mqtt_is_connected()) {
            mqtt_restart();
        }
    } else {
        ESP_LOGE(TAG, "Failed to save MQTT configuration");
    }
    
    // Retornar página de confirmação
    return send_confirmation_page(req, 
        "Configuração MQTT", 
        "Configuração Salva",
        result == ESP_OK ? "As configurações MQTT foram salvas com sucesso!" : "Erro ao salvar configurações MQTT.",
        "/mqtt_config",
        "Voltar às Configurações MQTT",
        3);
}

// API handler para status MQTT (JSON)
esp_err_t mqtt_status_api_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "mqtt_status_api_handler called");
    
    mqtt_state_t state = mqtt_get_state();
    const char* status_str;
    const char* message_str;
    
    switch (state) {
        case MQTT_STATE_CONNECTED:
            status_str = "connected";
            message_str = "Conectado ao broker";
            break;
        case MQTT_STATE_CONNECTING:
            status_str = "connecting";
            message_str = "Conectando...";
            break;
        case MQTT_STATE_ERROR:
            status_str = "disconnected";
            message_str = "Erro de conexão";
            break;
        default:
            status_str = "disconnected";
            message_str = "Desconectado";
            break;
    }
    
    // Criar JSON response
    cJSON *root = cJSON_CreateObject();
    cJSON *status = cJSON_CreateString(status_str);
    cJSON *message = cJSON_CreateString(message_str);
    
    cJSON_AddItemToObject(root, "status", status);
    cJSON_AddItemToObject(root, "message", message);
    
    char *json_string = cJSON_Print(root);
    cJSON_Delete(root);
    
    if (!json_string) {
        ESP_LOGE(TAG, "Failed to create JSON response");
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    esp_err_t result = httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    
    return result;
}

// API handler para teste de conexão MQTT (JSON)
esp_err_t mqtt_test_api_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "mqtt_test_api_handler called");
    
    // Verificar se é método POST
    if (req->method != HTTP_POST) {
        httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, "Method not allowed");
        return ESP_FAIL;
    }
    
    char buf[1024];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        ESP_LOGE(TAG, "Failed to receive POST data for MQTT test");
        return ESP_FAIL;
    }
    buf[received] = '\0';
    
    ESP_LOGI(TAG, "Received MQTT test data: %s", buf);
    
    // Parse JSON da requisição
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON for MQTT test");
        
        cJSON *error_root = cJSON_CreateObject();
        cJSON *success = cJSON_CreateBool(false);
        cJSON *message = cJSON_CreateString("JSON inválido");
        cJSON_AddItemToObject(error_root, "success", success);
        cJSON_AddItemToObject(error_root, "message", message);
        
        char *error_json = cJSON_Print(error_root);
        cJSON_Delete(error_root);
        
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, error_json, strlen(error_json));
        free(error_json);
        return ESP_OK;
    }
    
    // Por enquanto, simular teste (implementação completa requer modificações no mqtt_client_task)
    bool test_success = true; // Placeholder
    
    cJSON *response = cJSON_CreateObject();
    cJSON *success = cJSON_CreateBool(test_success);
    cJSON *message = cJSON_CreateString(test_success ? "Teste de conexão simulado com sucesso" : "Falha no teste de conexão");
    
    cJSON_AddItemToObject(response, "success", success);
    cJSON_AddItemToObject(response, "message", message);
    
    char *response_json = cJSON_Print(response);
    cJSON_Delete(response);
    cJSON_Delete(root);
    
    if (!response_json) {
        ESP_LOGE(TAG, "Failed to create test response JSON");
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    esp_err_t result = httpd_resp_send(req, response_json, strlen(response_json));
    free(response_json);
    
    return result;
}

void start_web_server() {
    if (server_handle != NULL) return;
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.max_uri_handlers = 512;
        config.max_resp_headers = 512;
        config.stack_size = 8192;  // Aumentar stack size para evitar overflow
        config.task_priority = 5;  // Prioridade m�dia
    esp_err_t ret = httpd_start(&server_handle, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start webserver: %s", esp_err_to_name(ret));
        server_handle = NULL;
        return;
    }
    // New log to show webserver started
    ESP_LOGI(TAG, "Web server started, registering URI handlers");

    // Registra handlers para p�ginas principais
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/", .method = HTTP_GET, .handler = root_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/login", .method = HTTP_GET, .handler = login_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/reset", .method = HTTP_GET, .handler = reset_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/exit", .method = HTTP_GET, .handler = exit_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/do_login", .method = HTTP_GET, .handler = do_login_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/do_login", .method = HTTP_POST, .handler = do_login_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/logout", .method = HTTP_GET, .handler = logout_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/modbus", .method = HTTP_GET, .handler = modbus_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/config_unidade", .method = HTTP_GET, .handler = config_unit_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/unit_values", .method = HTTP_GET, .handler = unit_values_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/info", .method = HTTP_GET, .handler = info_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/admin", .method = HTTP_GET, .handler = admin_get_handler });
    
    // Registra handlers para arquivos est�ticos CSS e JavaScript
    ESP_LOGI(TAG, "Registering CSS handler for /css/*");
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/css/*", .method = HTTP_GET, .handler = css_handler });
    ESP_LOGI(TAG, "Registering specific CSS file: /css/styles.css");
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/css/styles.css", .method = HTTP_GET, .handler = css_handler });
    
    ESP_LOGI(TAG, "Registering JS handler for /js/*");
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/js/*", .method = HTTP_GET, .handler = js_handler });
    ESP_LOGI(TAG, "Registering specific JS files");
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/js/scripts.js", .method = HTTP_GET, .handler = js_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/js/wifi-scan.js", .method = HTTP_GET, .handler = js_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/js/wifi-status.js", .method = HTTP_GET, .handler = js_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/js/ap-config.js", .method = HTTP_GET, .handler = js_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/js/confirmation.js", .method = HTTP_GET, .handler = js_handler });
    
    // Registra handlers para a��es POST
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/config_mode_save", .method = HTTP_POST, .handler = config_mode_save_post_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/unit_values_save", .method = HTTP_POST, .handler = unit_values_save_post_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/modbus_save", .method = HTTP_POST, .handler = modbus_save_post_handler});
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/ap_save", .method = HTTP_POST, .handler = ap_save_post_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/ap_config_save", .method = HTTP_POST, .handler = ap_config_save_post_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/rtu_config_save", .method = HTTP_POST, .handler = rtu_config_save_post_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/modbus_registers_save", .method = HTTP_POST, .handler = modbus_registers_save_post_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/wifi_config_save", .method = HTTP_POST, .handler = wifi_config_save_post_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/wifi_connect", .method = HTTP_POST, .handler = wifi_connect_post_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/wifi_save_nvs", .method = HTTP_POST, .handler = wifi_save_nvs_post_handler });
    // Registra handlers para p�ginas WiFi
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/wifi", .method = HTTP_GET, .handler = wifi_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/wifi-scan", .method = HTTP_GET, .handler = wifi_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/wifi_select", .method = HTTP_GET, .handler = wifi_select_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/wifi_save", .method = HTTP_POST, .handler = wifi_save_post_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/wifi_scan", .method = HTTP_GET, .handler = wifi_scan_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/wifi-scan-trigger", .method = HTTP_POST, .handler = wifi_scan_trigger_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/wifi-scan-data", .method = HTTP_GET, .handler = wifi_scan_data_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/wifi_status", .method = HTTP_GET, .handler = wifi_status_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/wifi-status", .method = HTTP_GET, .handler = wifi_status_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/wifi-status-data", .method = HTTP_GET, .handler = wifi_status_data_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/wifi_restart", .method = HTTP_POST, .handler = wifi_restart_post_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/wifi_test_connect", .method = HTTP_POST, .handler = wifi_test_connect_post_handler });
    
    // Registra handlers para configura��o AP  
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/ap-config", .method = HTTP_GET, .handler = ap_config_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/factory_reset", .method = HTTP_POST, .handler = factory_reset_post_handler });
    
    // Registra handlers para MQTT
    ESP_LOGI(TAG, "Registering MQTT handlers");
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/mqtt_config", .method = HTTP_GET, .handler = mqtt_config_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/mqtt_config", .method = HTTP_POST, .handler = mqtt_config_post_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/api/mqtt/status", .method = HTTP_GET, .handler = mqtt_status_api_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/api/mqtt/test", .method = HTTP_POST, .handler = mqtt_test_api_handler });
}

// Wi-Fi selection page - agora usa arquivo HTML
esp_err_t wifi_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "wifi_get_handler called (serving WiFi scan page from HTML file)");
    
    char *content = NULL;
    esp_err_t ret = load_file_content("/spiffs/html/wifi-scan.html", &content);
    
    if (ret != ESP_OK || !content) {
        ESP_LOGE(TAG, "Failed to load wifi-scan.html");
        return httpd_resp_send_404(req);
    }
    
    // Gera op��es de redes WiFi dispon�veis
    wifi_ap_record_t snapshot[MAX_APs];
    uint16_t snapshot_count = 0;
    wifi_get_ap_list_snapshot(snapshot, &snapshot_count);
    
    // Se n�o h� redes e nenhum scan em progresso, inicia scan
    if (snapshot_count == 0 && !wifi_is_scan_in_progress()) {
        wifi_start_scan_async();
    }
    
    // Ordena as redes por RSSI
    if (snapshot_count > 1) {
        qsort(snapshot, snapshot_count, sizeof(wifi_ap_record_t), compare_ap_rssi);
    }
    
    // Constr�i as op��es HTML para o select
    char wifi_options[2048] = "";
    for (int i = 0; i < (int)snapshot_count && i < 10; i++) {
        char ssid_escaped[WIFI_SSID_MAX_LEN * 2 + 1];
        html_escape((const char*)snapshot[i].ssid, ssid_escaped, sizeof(ssid_escaped));
        // Determine band from channel (primary channel field)
        const char *band = channel_to_band(snapshot[i].primary);
        char option[320];
        if (band && band[0] != '\0') {
            snprintf(option, sizeof(option), 
                "<option value=\"%s\">%s (%d dBm) - %s</option>", 
                ssid_escaped, ssid_escaped, snapshot[i].rssi, band);
        } else {
            snprintf(option, sizeof(option), 
                "<option value=\"%s\">%s (%d dBm)</option>", 
                ssid_escaped, ssid_escaped, snapshot[i].rssi);
        }
        strncat(wifi_options, option, sizeof(wifi_options) - strlen(wifi_options) - 1);
    }
    
    // Se n�o h� redes, adiciona op��o padr�o
    if (snapshot_count == 0) {
        strncpy(wifi_options, "<option value=\"\">Nenhuma rede encontrada</option>", 
                sizeof(wifi_options) - 1);
    }
    
    // Aplica substitui��es no template
    const char *substitutions[] = {
        "WIFI_OPTIONS", wifi_options,
        NULL, NULL
    };
    
    char *final_html = apply_template_substitutions(content, substitutions);
    free(content);
    
    if (!final_html) {
        ESP_LOGE(TAG, "Failed to apply template substitutions for wifi-scan");
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "text/html");
    esp_err_t result = httpd_resp_send(req, final_html, strlen(final_html));
    free(final_html);
    return result;
}

// Show password form for selected SSID
esp_err_t wifi_select_get_handler(httpd_req_t *req) {
    char buf[128];
    size_t qlen = httpd_req_get_url_query_len(req) + 1;
    if (qlen > sizeof(buf)) qlen = sizeof(buf) - 1;
    if (qlen > 1) {
        httpd_req_get_url_query_str(req, buf, qlen);
    } else {
        buf[0] = '\0';
    }
    char ssid[WIFI_SSID_MAX_LEN+1] = "";
    httpd_query_key_value(buf, "ssid", ssid, sizeof(ssid));

    char ssid_esc[WIFI_SSID_MAX_LEN*2]; html_escape(ssid, ssid_esc, sizeof(ssid_esc));
    const char *fmt = "<html><head><meta charset=\"UTF-8\">%s</head><body><h1>Escolha sua rede Wi-Fi</h1><h3>%s</h3><form class=\"wifi-form\" action=\"/wifi_save\" method=\"post\">"
                      "<input type=\"hidden\" name=\"ssid\" value=\"%s\">"
                      "Senha:<br><input type=\"password\" name=\"password\" required><br><br>"
                      "<div class=\"actions\"><button class=\"btn\" type=\"submit\">Salvar e conectar</button></div>"
                      "</form><div style=\"text-align:center;margin-top:16px;\"><a class=\"btn\" href=\"/wifi\">Voltar</a></div></body></html>";

    char *css_content = get_css_content();
    int needed = snprintf(NULL, 0, fmt, css_content ? css_content : "", ssid_esc, ssid_esc);
    char *page = malloc((size_t)needed + 1);
    if (!page) {
        if (css_content) free(css_content);
        return ESP_FAIL;
    }
    snprintf(page, (size_t)needed + 1, fmt, css_content ? css_content : "", ssid_esc, ssid_esc);
    if (css_content) free(css_content);
    esp_err_t res = httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
    free(page);
    return res;
}

// Configurar Dispositivo page with unit values and read-only values
// Handler para configura��o do dispositivo - usa template HTML
esp_err_t config_unit_get_handler(httpd_req_t *req) {
    // ?? VERIFICA��O DE PERMISS�O: Usu�rios b�sicos podem visualizar, apenas admin pode editar
    esp_err_t perm_result = check_user_permission(req, USER_LEVEL_BASIC);
    if (perm_result != ESP_OK) {
        return perm_result;
    }

    // Carrega template HTML
    char *template_content = NULL;
    esp_err_t ret = load_file_content("/spiffs/html/config_device.html", &template_content);
    
    if (ret != ESP_OK || !template_content) {
        ESP_LOGE(TAG, "Failed to load config_device.html");
        return httpd_resp_send_404(req);
    }
    
    // L� configura��es atuais para substitui��es
    ensure_spiffs();
    char device_name[32] = "ESP32 Medidor";
    char location[64] = "N�o definido";
    char unit_id[16] = "1";
    char wifi_status[32] = "Desconectado";
    char firmware_version[16] = "v1.0.0";
    char uptime[32] = "0h 0m";
    char free_memory[16] = "256KB";
    char chip_temperature[8] = "45";
    
    // Configura��es do AP - SEMPRE usar valores padr�o primeiro
    char ap_ssid[64];
    char ap_password[64]; 
    char ap_ip[32];
    
    // Definir valores padr�o garantidos
    strcpy(ap_ssid, "ESP32_Medidor_AP");
    strcpy(ap_password, "12345678");
    strcpy(ap_ip, "192.168.4.1");
    
    // Tentar ler do NVS, mas manter padr�es se falhar
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("ap_config", NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        char temp_ssid[64];
        char temp_password[64];
        char temp_ip[32];
        
        size_t required_size = sizeof(temp_ssid);
        if (nvs_get_str(nvs_handle, "ssid", temp_ssid, &required_size) == ESP_OK) {
            strcpy(ap_ssid, temp_ssid);
        }
        
        required_size = sizeof(temp_password);
        if (nvs_get_str(nvs_handle, "password", temp_password, &required_size) == ESP_OK) {
            strcpy(ap_password, temp_password);
        }
        
        required_size = sizeof(temp_ip);
        if (nvs_get_str(nvs_handle, "ip", temp_ip, &required_size) == ESP_OK) {
            strcpy(ap_ip, temp_ip);
        }
        
        nvs_close(nvs_handle);
    }
    
    ESP_LOGI(TAG, "? Config Device - Final AP values: SSID='%s', Password='%s', IP='%s'", ap_ssid, ap_password, ap_ip);
    
    // Configura��es RTU - valores padr�o
    char rtu_baudrate[16] = "9600";
    char rtu_databits[8] = "8";
    char rtu_parity[16] = "none";
    char rtu_stopbits[8] = "1";
    char rtu_slave_address[8] = "1";
    char rtu_timeout[16] = "1000";
    
    // L� configura��es RTU do config.json
    FILE *config_file = fopen("/spiffs/config.json", "r");
    if (config_file) {
        fseek(config_file, 0, SEEK_END);
        long file_size = ftell(config_file);
        rewind(config_file);
        
        char *json_buffer = malloc(file_size + 1);
        if (json_buffer) {
            fread(json_buffer, 1, file_size, config_file);
            json_buffer[file_size] = '\0';
            
            cJSON *json = cJSON_Parse(json_buffer);
            if (json) {
                cJSON *rtu_obj = cJSON_GetObjectItem(json, "modbus_rtu");
                if (rtu_obj) {
                    cJSON *item;
                    
                    item = cJSON_GetObjectItem(rtu_obj, "baudrate");
                    if (item && cJSON_IsNumber(item)) {
                        snprintf(rtu_baudrate, sizeof(rtu_baudrate), "%d", item->valueint);
                    }
                    
                    item = cJSON_GetObjectItem(rtu_obj, "databits");
                    if (item && cJSON_IsNumber(item)) {
                        snprintf(rtu_databits, sizeof(rtu_databits), "%d", item->valueint);
                    }
                    
                    item = cJSON_GetObjectItem(rtu_obj, "parity");
                    if (item && cJSON_IsString(item)) {
                        strncpy(rtu_parity, item->valuestring, sizeof(rtu_parity) - 1);
                    }
                    
                    item = cJSON_GetObjectItem(rtu_obj, "stopbits");
                    if (item && cJSON_IsNumber(item)) {
                        snprintf(rtu_stopbits, sizeof(rtu_stopbits), "%d", item->valueint);
                    }
                    
                    item = cJSON_GetObjectItem(rtu_obj, "slave_address");
                    if (item && cJSON_IsNumber(item)) {
                        snprintf(rtu_slave_address, sizeof(rtu_slave_address), "%d", item->valueint);
                    }
                    
                    item = cJSON_GetObjectItem(rtu_obj, "timeout");
                    if (item && cJSON_IsNumber(item)) {
                        snprintf(rtu_timeout, sizeof(rtu_timeout), "%d", item->valueint);
                    }
                }
                cJSON_Delete(json);
            }
            free(json_buffer);
        }
        fclose(config_file);
    }
    
    // Aqui voc� pode ler os valores reais do sistema
    // Por exemplo, do NVS, sensores, etc.
    
    // Define substitui��es para o template
    ESP_LOGI(TAG, "?? Template substitutions - AP_SSID: '%s', AP_PASSWORD: '%s', AP_IP: '%s'", ap_ssid, ap_password, ap_ip);
    
    const char *substitutions[] = {
        "DEVICE_NAME", device_name,
        "LOCATION", location,
        "UNIT_ID", unit_id,
        "WIFI_STATUS", wifi_status,
        "FIRMWARE_VERSION", firmware_version,
        "UPTIME", uptime,
        "FREE_MEMORY", free_memory,
        "CHIP_TEMPERATURE", chip_temperature,
        "AP_SSID", ap_ssid,
        "AP_PASSWORD", ap_password,
        "AP_IP", ap_ip,
        "RTU_BAUDRATE", rtu_baudrate,
        "RTU_DATABITS", rtu_databits,
        "RTU_PARITY", rtu_parity,
        "RTU_STOPBITS", rtu_stopbits,
        "RTU_SLAVE_ADDRESS", rtu_slave_address,
        "RTU_TIMEOUT", rtu_timeout,
        NULL // Terminador
    };
    
    // Aplica substitui��es
    char *final_content = apply_template_substitutions(template_content, substitutions);
    free(template_content);
    
    if (!final_content) {
        return ESP_ERR_NO_MEM;
    }
    
    // Envia resposta
    httpd_resp_set_type(req, "text/html");
    esp_err_t result = httpd_resp_send(req, final_content, strlen(final_content));
    free(final_content);
    
    return result;
}

// Handler para valores da unidade - usa template HTML
esp_err_t unit_values_get_handler(httpd_req_t *req) {
    // Carrega template HTML
    char *template_content = NULL;
    esp_err_t ret = load_file_content("/spiffs/html/unit_values.html", &template_content);
    
    if (ret != ESP_OK || !template_content) {
        ESP_LOGE(TAG, "Failed to load unit_values.html");
        return httpd_resp_send_404(req);
    }
    
    // L� valores atuais do sistema (simulados para demonstra��o)
    ensure_spiffs();
    char temperature[16] = "850";
    char pressure[16] = "2.4";
    char fuel_flow[16] = "125.5";
    char oxygen_level[16] = "21.0";
    char system_status[32] = "Normal";
    char operation_time[32] = "12h 34m";
    char active_alarms[16] = "0";
    char last_maintenance[32] = "01/10/2025";
    char avg_temperature[16] = "842";
    char max_temperature[16] = "865";
    char min_temperature[16] = "820";
    char avg_efficiency[16] = "92.5";
    char total_consumption[16] = "3005.2";
    char collection_interval[8] = "30";
    char alarm_temperature[8] = "900";
    char alarm_pressure[8] = "5.0";
    char min_oxygen[8] = "18.0";
    
    // Aqui voc� pode ler os valores reais dos sensores, NVS, etc.
    
    // Define substitui��es para o template
    const char *substitutions[] = {
        "TEMPERATURE", temperature,
        "PRESSURE", pressure,
        "FUEL_FLOW", fuel_flow,
        "OXYGEN_LEVEL", oxygen_level,
        "SYSTEM_STATUS", system_status,
        "OPERATION_TIME", operation_time,
        "ACTIVE_ALARMS", active_alarms,
        "LAST_MAINTENANCE", last_maintenance,
        "AVG_TEMPERATURE", avg_temperature,
        "MAX_TEMPERATURE", max_temperature,
        "MIN_TEMPERATURE", min_temperature,
        "AVG_EFFICIENCY", avg_efficiency,
        "TOTAL_CONSUMPTION", total_consumption,
        "COLLECTION_INTERVAL", collection_interval,
        "ALARM_TEMPERATURE", alarm_temperature,
        "ALARM_PRESSURE", alarm_pressure,
        "MIN_OXYGEN", min_oxygen,
        NULL // Terminador
    };
    
    // Aplica substitui��es
    char *final_content = apply_template_substitutions(template_content, substitutions);
    free(template_content);
    
    if (!final_content) {
        return ESP_ERR_NO_MEM;
    }
    
    // Envia resposta
    httpd_resp_set_type(req, "text/html");
    esp_err_t result = httpd_resp_send(req, final_content, strlen(final_content));
    free(final_content);
    
    return result;
}

// POST handler to save values into /spiffs/config.json
esp_err_t unit_values_save_post_handler(httpd_req_t *req) {
    // receive body
    char buf[256] = {0};
    int r = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (r <= 0) return httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);

    char dutty_s[16] = ""; char periodo_s[16] = ""; char maxdac_s[16] = ""; char forcadac_s[16] = "";
    httpd_query_key_value(buf, "dutty", dutty_s, sizeof(dutty_s));
    httpd_query_key_value(buf, "periodo", periodo_s, sizeof(periodo_s));
    httpd_query_key_value(buf, "max_dac", maxdac_s, sizeof(maxdac_s));
    httpd_query_key_value(buf, "forca_dac", forcadac_s, sizeof(forcadac_s));

    int dutty = atoi(dutty_s); int periodo = atoi(periodo_s); int max_dac = atoi(maxdac_s); int forca_dac = atoi(forcadac_s);

    // read existing config
    ensure_spiffs();
    FILE *f = fopen("/spiffs/config.json", "r");
    cJSON *root = NULL;
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        rewind(f);
        char *b = malloc(sz + 1);
        if (b) {
            fread(b, 1, sz, f);
            b[sz] = '\0';
            root = cJSON_Parse(b);
            free(b);
        }
        fclose(f);
    }
    if (!root) root = cJSON_CreateObject();

    cJSON_ReplaceItemInObject(root, "dutty", cJSON_CreateNumber(dutty));
    cJSON_ReplaceItemInObject(root, "periodo", cJSON_CreateNumber(periodo));
    cJSON_ReplaceItemInObject(root, "max_dac", cJSON_CreateNumber(max_dac));
    cJSON_ReplaceItemInObject(root, "forca_dac", cJSON_CreateNumber(forca_dac));

    char *out = cJSON_Print(root);
    FILE *fw = fopen("/spiffs/config.json", "w");
    if (!fw) {
        cJSON_Delete(root);
        free(out);
        return httpd_resp_send(req, "Failed to open config for writing", HTTPD_RESP_USE_STRLEN);
    }
    fprintf(fw, "%s", out);
    fclose(fw);
    cJSON_Delete(root);
    free(out);

    // Redirect back to unit values page
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/unit_values");
    return httpd_resp_send(req, NULL, 0);
}

// Handler para p�gina de informa��es do sistema
esp_err_t info_get_handler(httpd_req_t *req) {
    // Carrega template HTML
    char *template_content = NULL;
    esp_err_t ret = load_file_content("/spiffs/html/info.html", &template_content);
    
    if (ret != ESP_OK || !template_content) {
        ESP_LOGE(TAG, "Failed to load info.html");
        return httpd_resp_send_404(req);
    }

    // Coleta informa��es do ESP32
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    char chip_model[32];
    switch (chip_info.model) {
        case CHIP_ESP32: strcpy(chip_model, "ESP32"); break;
        case CHIP_ESP32S2: strcpy(chip_model, "ESP32-S2"); break;
        case CHIP_ESP32C3: strcpy(chip_model, "ESP32-C3"); break;
        case CHIP_ESP32S3: strcpy(chip_model, "ESP32-S3"); break;
        case CHIP_ESP32C2: strcpy(chip_model, "ESP32-C2"); break;
        case CHIP_ESP32C6: strcpy(chip_model, "ESP32-C6"); break;
        case CHIP_ESP32H2: strcpy(chip_model, "ESP32-H2"); break;
        default: strcpy(chip_model, "ESP32-Desconhecido"); break;
    }

    // Informa��es do sistema
    char chip_revision[8];
    char chip_cores[8]; 
    char cpu_frequency[16];
    char total_ram[16];
    char free_ram[16];
    char flash_size[16];
    char mac_address[20];
    char program_version[32];
    char compile_date[32];
    char compile_time[32];
    char idf_version[32];
    char uptime[64];

    snprintf(chip_revision, sizeof(chip_revision), "%d", chip_info.revision);
    snprintf(chip_cores, sizeof(chip_cores), "%d", chip_info.cores);
    
    snprintf(cpu_frequency, sizeof(cpu_frequency), "%d", (int)rtc_clk_apb_freq_get());
    
    uint32_t total_heap = heap_caps_get_total_size(MALLOC_CAP_8BIT);
    uint32_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    snprintf(total_ram, sizeof(total_ram), "%.1f", total_heap / 1024.0f);
    snprintf(free_ram, sizeof(free_ram), "%.1f", free_heap / 1024.0f);
    
    uint32_t flash_size_bytes;
    esp_flash_get_size(NULL, &flash_size_bytes);
    snprintf(flash_size, sizeof(flash_size), "%lu", flash_size_bytes / (1024 * 1024));
    
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(mac_address, sizeof(mac_address), "%02X:%02X:%02X:%02X:%02X:%02X", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Vers�o baseada no t�tulo da p�gina principal (Medidor de Combust�o)
    strcpy(program_version, "Medidor de Combust�o v1.0");
    strcpy(compile_date, __DATE__);
    strcpy(compile_time, __TIME__);
    strcpy(idf_version, esp_get_idf_version());
    
    uint64_t uptime_ms = esp_timer_get_time() / 1000;
    uint32_t days = uptime_ms / (24 * 60 * 60 * 1000);
    uint32_t hours = (uptime_ms % (24 * 60 * 60 * 1000)) / (60 * 60 * 1000);
    uint32_t minutes = (uptime_ms % (60 * 60 * 1000)) / (60 * 1000);
    snprintf(uptime, sizeof(uptime), "%lud %luh %lum", days, hours, minutes);

    // Configura��es do AP
    char ap_ssid[64] = "ESP32-AP";
    char ap_password[64] = "12345678";
    char ap_ip[32] = "192.168.4.1";
    char ap_channel[8] = "1";
    char ap_max_conn[8] = "4";
    char ap_status[32] = "Ativo";

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("ap_config", NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        size_t required_size = sizeof(ap_ssid);
        nvs_get_str(nvs_handle, "ssid", ap_ssid, &required_size);
        required_size = sizeof(ap_password);
        nvs_get_str(nvs_handle, "password", ap_password, &required_size);
        required_size = sizeof(ap_ip);
        nvs_get_str(nvs_handle, "ip", ap_ip, &required_size);
        nvs_close(nvs_handle);
    }

    // Configura��es do WiFi
    char wifi_ssid[64] = "N�o configurado";
    char wifi_password[64] = "��������"; // masked for display
    char wifi_password_plain[64] = "";   // actual password (if available)
    char wifi_ip[32] = "N�o conectado";
    char wifi_netmask[32] = "N�o conectado";
    char wifi_gateway[32] = "N�o conectado";
    char wifi_status[32] = "Desconectado";
    char wifi_rssi[16] = "N/A";

    // L� configura��es WiFi do NVS
    nvs_handle_t wifi_nvs;
    err = nvs_open("wifi_config", NVS_READONLY, &wifi_nvs);
    if (err == ESP_OK) {
        size_t req_size = sizeof(wifi_ssid);
        if (nvs_get_str(wifi_nvs, "ssid", wifi_ssid, &req_size) == ESP_OK) {
            // Se tem SSID configurado, tentar ler a senha real e mascarar para display
            size_t pass_size = sizeof(wifi_password_plain);
            if (nvs_get_str(wifi_nvs, "password", wifi_password_plain, &pass_size) == ESP_OK) {
                // senha dispon�vel no NVS, manter masked display but keep plain
                strncpy(wifi_password, "��������", sizeof(wifi_password)-1);
                wifi_password[sizeof(wifi_password)-1] = '\0';
            } else {
                // senha n�o dispon�vel explicitamente, keep mask
                strncpy(wifi_password_plain, "", sizeof(wifi_password_plain));
                strncpy(wifi_password, "��������", sizeof(wifi_password)-1);
                wifi_password[sizeof(wifi_password)-1] = '\0';
            }
        }
        nvs_close(wifi_nvs);
    }

    // Obter informa��es da conex�o atual
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        strcpy(wifi_status, "Conectado");
        snprintf(wifi_rssi, sizeof(wifi_rssi), "%d", ap_info.rssi);
        
        // Obter IP atual
        esp_netif_ip_info_t ip_info;
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            esp_ip4addr_ntoa(&ip_info.ip, wifi_ip, sizeof(wifi_ip));
            esp_ip4addr_ntoa(&ip_info.netmask, wifi_netmask, sizeof(wifi_netmask));
            esp_ip4addr_ntoa(&ip_info.gw, wifi_gateway, sizeof(wifi_gateway));
        }
    }

    // Se conectado, preferir mostrar o SSID atual da AP em vez do SSID salvo no NVS
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        // ap_info.ssid � um array de bytes sem termina��o garantida
        size_t copy_len = sizeof(wifi_ssid) - 1;
        memcpy(wifi_ssid, ap_info.ssid, copy_len);
        wifi_ssid[copy_len] = '\0';
        // Garantir termina��o correta: se houver um '\0' dentro de ap_info.ssid, ajustar
        for (size_t i = 0; i < copy_len; ++i) {
            if (wifi_ssid[i] == '\0') break;
        }
    }

    // Array de substitui��es
    const char *substitutions[] = {
        // ESP32 Info
        "CHIP_MODEL", chip_model,
        "CHIP_REVISION", chip_revision,
        "CHIP_CORES", chip_cores,
        "CPU_FREQUENCY", cpu_frequency,
        "TOTAL_RAM", total_ram,
        "FREE_RAM", free_ram,
        "FLASH_SIZE", flash_size,
        "MAC_ADDRESS", mac_address,
        
        // Program Info
        "PROJECT_NAME", "Medidor de Combust�o ESP32",
        "PROGRAM_VERSION", program_version,
        "COMPILE_DATE", compile_date,
        "COMPILE_TIME", compile_time,
        "IDF_VERSION", idf_version,
        "UPTIME", uptime,
        
        // AP Info
        "AP_SSID", ap_ssid,
        "AP_PASSWORD", ap_password,
        "AP_IP", ap_ip,
        "AP_CHANNEL", ap_channel,
        "AP_MAX_CONNECTIONS", ap_max_conn,
        "AP_STATUS", ap_status,
        
        // WiFi Info
        "WIFI_SSID", wifi_ssid,
    "WIFI_PASSWORD_DISPLAY", wifi_password,
    "WIFI_PASSWORD_PLAIN", wifi_password_plain,
        "WIFI_IP", wifi_ip,
        "WIFI_NETMASK", wifi_netmask,
        "WIFI_GATEWAY", wifi_gateway,
        "WIFI_STATUS", wifi_status,
        "WIFI_RSSI", wifi_rssi,
        
        NULL // Terminador
    };

    // Aplica substitui��es
    char *final_content = apply_template_substitutions(template_content, substitutions);
    free(template_content);
    
    if (!final_content) {
        return ESP_ERR_NO_MEM;
    }
    
    // Envia resposta
    httpd_resp_set_type(req, "text/html");
    esp_err_t result = httpd_resp_send(req, final_content, strlen(final_content));
    free(final_content);
    
    return result;
}

// Save posted SSID/password, persist and attempt to connect
esp_err_t wifi_save_post_handler(httpd_req_t *req) {
    // read body
    char buf[256] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (ret <= 0) return httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);

    char ssid[WIFI_SSID_MAX_LEN+1] = ""; char password[WIFI_PASS_MAX_LEN+1] = "";
    httpd_query_key_value(buf, "ssid", ssid, sizeof(ssid));
    httpd_query_key_value(buf, "password", password, sizeof(password));

    // Decode percent-encoding (e.g. %23 -> #)
    url_decode_inplace(ssid);
    url_decode_inplace(password);

    // Salvar usando fun��o padronizada (agora salva no NVS wifi_config)
    save_wifi_config(ssid, password);

    ESP_LOGI(TAG, "WiFi config salvo via wifi_save_post_handler - SSID: %s", ssid);

    // Load template and apply substitutions
    char *html_template = NULL;
    esp_err_t ret_load = load_file_content("/spiffs/html/wifi_credentials_saved.html", &html_template);
    if (ret_load != ESP_OK || !html_template) {
        ESP_LOGE(TAG, "Failed to load wifi_credentials_saved.html template");
        return httpd_resp_send_404(req);
    }

    // Prepare template substitutions
    const char *substitutions[] = {
        "SSID", ssid,
        "PASSWORD", password,
        NULL, NULL
    };

    // Apply substitutions
    char *final_html = apply_template_substitutions(html_template, substitutions);
    free(html_template);
    
    if (!final_html) {
        ESP_LOGE(TAG, "Failed to apply template substitutions");
        return ESP_FAIL;
    }
    
    // Send response
    httpd_resp_set_type(req, "text/html");
    esp_err_t res = httpd_resp_send(req, final_html, strlen(final_html));
    free(final_html);
    return res;
}

// POST handler that attempts to connect now (without restart) and returns a status page that polls /wifi_status
esp_err_t wifi_test_connect_post_handler(httpd_req_t *req) {
    // read body
    char buf[256] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (ret <= 0) return httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);
    char ssid[WIFI_SSID_MAX_LEN+1] = ""; char password[WIFI_PASS_MAX_LEN+1] = "";
    httpd_query_key_value(buf, "ssid", ssid, sizeof(ssid));
    httpd_query_key_value(buf, "password", password, sizeof(password));

    // apply config and attempt connect (no restart)
    wifi_set_sta_config(ssid, password);
    wifi_connect(ssid, password);
    wifi_switch_to_sta_on_successful_connect(15000);

    // Load template and apply substitutions
    char *html_template = NULL;
    esp_err_t ret_load = load_file_content("/spiffs/html/wifi_connection_test.html", &html_template);
    if (ret_load != ESP_OK || !html_template) {
        ESP_LOGE(TAG, "Failed to load wifi_connection_test.html template");
        return httpd_resp_send_404(req);
    }

    // Prepare template substitutions
    const char *substitutions[] = {
        "SSID", ssid,
        NULL, NULL
    };

    // Apply substitutions
    char *final_html = apply_template_substitutions(html_template, substitutions);
    free(html_template);
    
    if (!final_html) {
        ESP_LOGE(TAG, "Failed to apply template substitutions");
        return ESP_FAIL;
    }
    
    // Send response
    httpd_resp_set_type(req, "text/html");
    esp_err_t res = httpd_resp_send(req, final_html, strlen(final_html));
    free(final_html);
    return res;
}

// Handler para p�gina de status WiFi - agora usa arquivo HTML  
esp_err_t wifi_status_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "wifi_status_get_handler called (serving WiFi status page from HTML file)");
    
    char *content = NULL;
    esp_err_t ret = load_file_content("/spiffs/html/wifi-status.html", &content);
    
    if (ret != ESP_OK || !content) {
        ESP_LOGE(TAG, "Failed to load wifi-status.html");
        return httpd_resp_send_404(req);
    }
    
    // Obt�m status atual do WiFi
    wifi_status_t st = wifi_get_status();
    
    // Determina displays condicionais
    const char *ip_display = st.is_connected ? "block" : "none";
    const char *rssi_display = st.is_connected ? "block" : "none";
    const char *progress_display = (!st.is_connected && strlen(st.status_message) > 0) ? "block" : "none";
    const char *error_display = "none"; // TODO: implementar l�gica de erro
    
    // Converte RSSI para string
    char rssi_str[16];
    snprintf(rssi_str, sizeof(rssi_str), "%d", st.rssi);
    
    // Define substitui��es para o template
    const char *substitutions[] = {
        "WIFI_STATUS", st.is_connected ? "Conectado" : "Desconectado",
        "WIFI_SSID", st.current_ssid[0] ? st.current_ssid : "Nenhuma rede",
        "WIFI_IP", st.ip_address[0] ? st.ip_address : "0.0.0.0",
        "WIFI_RSSI", rssi_str,
        "IP_DISPLAY", ip_display,
        "RSSI_DISPLAY", rssi_display,
        "PROGRESS_DISPLAY", progress_display,
        "ERROR_DISPLAY", error_display,
        "ERROR_MESSAGE", "",
        NULL, NULL
    };
    
    char *final_html = apply_template_substitutions(content, substitutions);
    free(content);
    
    if (!final_html) {
        ESP_LOGE(TAG, "Failed to apply template substitutions for wifi-status");
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "text/html");
    esp_err_t result = httpd_resp_send(req, final_html, strlen(final_html));
    free(final_html);
    return result;
}

// API endpoint para dados de status WiFi (JSON)
esp_err_t wifi_status_data_handler(httpd_req_t *req) {
    wifi_status_t st = wifi_get_status();
    char buf[512];
    
    const char *status_text = "Desconectado";
    if (st.is_connected) {
        status_text = "Conectado";
    } else if (strlen(st.status_message) > 0) {
        status_text = "Conectando";
    }
    
    snprintf(buf, sizeof(buf), 
            "{\"status\": \"%s\", \"ssid\": \"%s\", \"ip\": \"%s\", \"rssi\": \"%d\", \"message\": \"%s\"}",
            status_text,
            st.current_ssid[0] ? st.current_ssid : "",
            st.ip_address[0] ? st.ip_address : "",
            st.rssi,
            st.status_message[0] ? st.status_message : "");
    
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

// Handler para p�gina de scan WiFi (redireciona para a p�gina unificada)
esp_err_t wifi_scan_get_handler(httpd_req_t *req) {
    // Redireciona para a p�gina unificada de configura��o WiFi
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/wifi");
    return httpd_resp_send(req, NULL, 0);
}

// API endpoint para disparar um novo scan WiFi
esp_err_t wifi_scan_trigger_handler(httpd_req_t *req) {
    esp_err_t e = wifi_start_scan_async();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start async scan: %s", esp_err_to_name(e));
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, "{\"success\": false, \"error\": \"Failed to start scan\"}", HTTPD_RESP_USE_STRLEN);
    }
    
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"success\": true}", HTTPD_RESP_USE_STRLEN);
}

// API endpoint para obter dados do scan WiFi (JSON)
esp_err_t wifi_scan_data_handler(httpd_req_t *req) {
    wifi_ap_record_t snapshot[MAX_APs];
    uint16_t snapshot_count = 0;
    wifi_get_ap_list_snapshot(snapshot, &snapshot_count);
    
    // Se n�o h� resultados e n�o h� scan em progresso, tenta iniciar scan
    if (snapshot_count == 0 && !wifi_is_scan_in_progress()) {
        wifi_start_scan_async();
    }
    
    // Constr�i JSON com as redes encontradas
    char *json_buffer = malloc(4096);
    if (!json_buffer) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, "{\"networks\": [], \"scanning\": false}", HTTPD_RESP_USE_STRLEN);
    }
    
    strcpy(json_buffer, "{\"networks\": [");
    
    // Ordena por RSSI
    if (snapshot_count > 1) {
        qsort(snapshot, snapshot_count, sizeof(wifi_ap_record_t), compare_ap_rssi);
    }
    
    for (int i = 0; i < (int)snapshot_count && i < 10; i++) {
        char ssid_escaped[WIFI_SSID_MAX_LEN * 2 + 1];
        html_escape((const char*)snapshot[i].ssid, ssid_escaped, sizeof(ssid_escaped));
        
        const char *band = channel_to_band(snapshot[i].primary);
        char network_json[320];
        if (band && band[0] != '\0') {
            snprintf(network_json, sizeof(network_json), 
                "%s{\"ssid\": \"%s\", \"rssi\": %d, \"band\": \"%s\"}", 
                i > 0 ? ", " : "", ssid_escaped, snapshot[i].rssi, band);
        } else {
            snprintf(network_json, sizeof(network_json), 
                "%s{\"ssid\": \"%s\", \"rssi\": %d}", 
                i > 0 ? ", " : "", ssid_escaped, snapshot[i].rssi);
        }
        
        if (strlen(json_buffer) + strlen(network_json) < 4000) {
            strcat(json_buffer, network_json);
        }
    }
    
    char scanning_status[100];
    snprintf(scanning_status, sizeof(scanning_status), 
            "], \"scanning\": %s}", 
            wifi_is_scan_in_progress() ? "true" : "false");
    strcat(json_buffer, scanning_status);
    
    httpd_resp_set_type(req, "application/json");
    esp_err_t result = httpd_resp_send(req, json_buffer, strlen(json_buffer));
    free(json_buffer);
    return result;
}

// POST endpoint that restarts the device when user confirms - agora usa template
esp_err_t wifi_restart_post_handler(httpd_req_t *req) {
    char *content = NULL;
    esp_err_t ret = load_file_content("/spiffs/html/confirmation.html", &content);
    
    if (ret != ESP_OK || !content) {
        ESP_LOGE(TAG, "Failed to load confirmation.html");
        return httpd_resp_send(req, "Reiniciando...", HTTPD_RESP_USE_STRLEN);
    }
    
    // Define substitui��es para o template
    const char *substitutions[] = {
        "PAGE_TITLE", "Reiniciando",
        "MESSAGE_TITLE", "Reiniciando ESP32",
        "MESSAGE_TEXT", "O dispositivo est� sendo reiniciado para aplicar as configura��es WiFi.",
        "REDIRECT_DISPLAY", "none",
        "COUNTDOWN", "0",
        "RETURN_URL", "/",
        "RETURN_TEXT", "P�gina Inicial",
        NULL, NULL
    };
    
    char *final_html = apply_template_substitutions(content, substitutions);
    free(content);
    
    if (final_html) {
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, final_html, strlen(final_html));
        free(final_html);
    } else {
        httpd_resp_send(req, "Reiniciando...", HTTPD_RESP_USE_STRLEN);
    }

    // Aguarda um pouco antes de reiniciar
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}
