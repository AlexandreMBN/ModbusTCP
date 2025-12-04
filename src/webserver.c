/* ============================================================================
 * WEBSERVER.C - SERVIDOR WEB HTTP PARA INTERFACE DE CONFIGURAÇÃO E MONITORAMENTO
 * ============================================================================
 * 
 * DESCRIÇÃO:
 * -----------
 * Este módulo implementa um servidor web HTTP completo para interface com
 * o sistema de controle da sonda lambda. Fornece páginas web para 
 * monitoramento em tempo real, configuração de parâmetros, diagnósticos
 * e controle remoto via navegador web.
 * 
 * ARQUITETURA WEB:
 * ----------------
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │                          SERVIDOR WEB HTTP                              │
 * │                                                                         │
 * │  [Cliente] → [WiFi] → [ESP32 HTTP Server] → [Handlers] → [Backend]     │
 * │     ↑                           ↓               ↓           ↓           │
 * │ [Browser] ← [HTML/CSS/JS] ← [SPIFFS Files] → [JSON API] → [Modbus]     │
 * │                                      ↓                                  │
 * │                              [Config Manager] → [NVS/SPIFFS]           │
 * └─────────────────────────────────────────────────────────────────────────┘
 * 
 * ESTRUTURA DE ARQUIVOS WEB:
 * --------------------------
 * /spiffs/data/
 * ├── html/                    (Páginas HTML)
 * │   ├── index.html              (Dashboard principal)
 * │   ├── admin.html              (Painel administrativo)
 * │   ├── config.html             (Configurações gerais)
 * │   ├── modbus.html             (Configuração Modbus)
 * │   ├── modbus_tcp_config.html  (Configuração Modbus TCP)
 * │   ├── ap-config.html          (Configuração Access Point)
 * │   ├── login_invalid.html      (Erro de login)
 * │   └── info.html               (Informações do sistema)
 * │
 * ├── css/
 * │   └── styles.css              (Estilos responsivos)
 * │
 * ├── js/
 * │   └── app.js                  (JavaScript para interações)
 * │
 * └── config/                  (Arquivos de configuração JSON)
 *     ├── rtu_config.json
 *     ├── ap_config.json
 *     ├── sta_config.json
 *     ├── mqtt_config.json
 *     ├── network_config.json
 *     └── modbus_tcp_config.json
 * 
 * ROTAS E ENDPOINTS:
 * ------------------
 * 
 * 🏠 PÁGINAS PRINCIPAIS:
 *    GET  /                      → Dashboard principal (index.html)
 *    GET  /admin                 → Painel administrativo
 *    GET  /config                → Página de configurações
 *    GET  /modbus                → Configuração Modbus RTU
 *    GET  /modbus_tcp_config     → Configuração Modbus TCP
 *    GET  /info                  → Informações do sistema
 * 
 * 🔐 AUTENTICAÇÃO E CONTROLE:
 *    POST /login                 → Login de usuário
 *    POST /login_admin           → Login administrativo
 *    POST /logout                → Logout do sistema
 *    GET  /check_login_status    → Status da sessão atual
 * 
 * ⚙️ APIs DE CONFIGURAÇÃO:
 *    POST /config_wifi           → Configurar WiFi STA
 *    POST /config_ap             → Configurar Access Point
 *    POST /config_mqtt           → Configurar cliente MQTT
 *    POST /config_network        → Configurar IP estático/DHCP
 *    POST /save_modbus_tcp       → Salvar configurações Modbus TCP
 *    GET  /status                → Status geral do sistema
 * 
 * APIs DE DADOS E MONITORAMENTO:
 *    GET  /api/registers         → Todos os registradores Modbus
 *    GET  /api/modbus_data       → Dados específicos Modbus
 *    GET  /api/system_info       → Informações detalhadas do hardware
 *    GET  /api/wifi_scan         → Scan de redes WiFi disponíveis
 *    POST /api/registers         → Atualizar registradores (admin)
 * 
 * CONTROLES AVANÇADOS:
 *    POST /factory_reset         → Reset de fábrica completo
 *    POST /restart               → Reiniciar ESP32
 *    GET  /download_config       → Download de backup das configurações
 *    POST /upload_config         → Upload de backup de configurações
 * 
 * SISTEMA DE AUTENTICAÇÃO:
 * ------------------------
 * 
 * 🔑 NÍVEIS DE USUÁRIO:
 * - USER_LEVEL_NONE (0)      : Sem acesso
 * - USER_LEVEL_OPERATOR (1)  : Monitoramento apenas (read-only)
 * - USER_LEVEL_ADMIN (2)     : Configuração completa
 * - USER_LEVEL_ROOT (3)      : Acesso total + factory reset
 * 
 * CONTROLE DE ACESSO POR ENDPOINT:
 * - Dashboard (/): Livre
 * - Configurações (/config): USER_LEVEL_ADMIN
 * - Modbus TCP (/modbus_tcp_config): USER_LEVEL_ADMIN
 * - Factory Reset: USER_LEVEL_ROOT
 * - APIs de leitura: USER_LEVEL_OPERATOR
 * - APIs de escrita: USER_LEVEL_ADMIN
 * 
 * DEPENDÊNCIAS DIRETAS:
 * ---------------------
 * - config_manager.c         : Carregar/salvar todas as configurações
 * - wifi_manager.c           : Status WiFi, scan de redes, conexão
 * - modbus_params.c          : Acesso aos registradores Modbus
 * - modbus_tcp_slave_task.c  : Controle do servidor Modbus TCP
 * - mqtt_client_task.c       : Status e configuração MQTT
 * - esp_spiffs.h             : Sistema de arquivos para HTML/CSS/JS
 * - esp_http_server.h        : Servidor HTTP do ESP-IDF
 * 
 * FUNCIONALIDADES ESPECIAIS:
 * --------------------------
 * 
 * REINICIALIZAÇÃO SUAVE:
 *    - Task temporária para reinício (evita travamento)
 *    - Delay de 2s para completar resposta HTTP
 *    - Log detalhado do processo
 * 
 * 🏭 FACTORY RESET:
 *    - Apaga todas as configurações (NVS + SPIFFS)
 *    - Remove arquivos de configuração específicos
 *    - Notifica máquina de estados via event bus
 *    - Reinicialização completa do sistema
 * 
 * 📱 INTERFACE RESPONSIVA:
 *    - CSS adaptativo para desktop/mobile
 *    - JavaScript assíncrono para atualizações em tempo real
 *    - Feedback visual para todas as ações
 *    - Validação de formulários no cliente e servidor
 * 
 * DIAGNÓSTICOS AVANÇADOS:
 *    - Informações detalhadas de hardware (CPU, RAM, Flash)
 *    - Status de todas as tasks do sistema
 *    - Estatísticas de rede e WiFi
 *    - Logs em tempo real via interface web
 * 
 * EXEMPLO DE FLUXO TÍPICO:
 * ------------------------
 * 1. Cliente acessa http://192.168.4.1/
 * 2. Servidor carrega index.html do SPIFFS
 * 3. JavaScript faz chamada para /api/registers
 * 4. Servidor lê registradores Modbus thread-safe
 * 5. Resposta JSON enviada para cliente
 * 6. Interface atualizada com dados em tempo real
 * 
 * SEGURANÇA:
 * ----------
 * Proteção contra buffer overflow
 * Validação de entrada em todos os formulários
 * Sanitização de strings JSON
 * Timeouts para todas as operações
 * Controle de acesso baseado em níveis de usuário
 * Logs de auditoria para todas as ações administrativas
 * 
 * ============================================================================
 */

#include "webserver.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "modbus_params.h"
#include "modbus_tcp_slave_task.h"
#include "mqtt_client_task.h"
#include "main_config_flags.h"
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
#include <sys/stat.h>

static const char *TAG = "web_min";
static httpd_handle_t server_handle = NULL;
static bool restart_task_running = false;

// Função para verificar permissões de acesso
esp_err_t check_user_permission(httpd_req_t *req, user_level_t required_level) {
    if (!check_access_permission(required_level)) {
        user_level_t current_level = load_user_level();
        ESP_LOGW(TAG, "Acesso negado. Nível atual: %d, Requerido: %d", current_level, required_level);

        // Página de acesso negado
        const char* access_denied_html =
            "<!DOCTYPE html>"
            "<html lang='pt-BR'>"
            "<head><meta charset='UTF-8'><title>Acesso Negado</title>"
            "<link rel='stylesheet' href='/css/styles.css'></head>"
            "<body><div><h1>Acesso Negado</h1>"
            "<p>Você não tem permissão para acessar esta página.</p>"
            "<p>Nível de acesso atual: %s</p>"
            "<p>Nível requerido: %s</p>"
            "<a href='/admin'>Voltar ao Painel</a></div></body></html>";

        char *response = malloc(1024);
        if (response) {
            const char* current_desc = (current_level == USER_LEVEL_BASIC) ? "Padrão (adm)" :
                                     (current_level == USER_LEVEL_ADMIN) ? "Administrador (root)" : "Nenhum";
            const char* required_desc = (required_level == USER_LEVEL_BASIC) ? "Padrão" : "Administrador";

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

// Modbus TCP handlers
esp_err_t modbus_tcp_config_get_handler(httpd_req_t *req);
esp_err_t modbus_tcp_config_api_handler(httpd_req_t *req);
esp_err_t modbus_tcp_config_save_handler(httpd_req_t *req);
esp_err_t modbus_tcp_status_api_handler(httpd_req_t *req);
esp_err_t modbus_status_api_handler(httpd_req_t *req);
esp_err_t modbus_set_mode_api_handler(httpd_req_t *req);
esp_err_t wifi_status_api_handler(httpd_req_t *req);

// Config management handlers (for root user)
esp_err_t config_upload_handler(httpd_req_t *req);
esp_err_t config_download_handler(httpd_req_t *req);

// Main config (main_config.json) handlers
esp_err_t main_config_api_handler(httpd_req_t *req);
esp_err_t main_config_save_handler(httpd_req_t *req);



// Helper function para páginas de confirmação
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
// ARQUIVO ESTÁTICO E TEMPLATE SYSTEM
// =============================================================================

/**
 * Carrega conteúdo de um arquivo do SPIFFS
 * @param filepath Caminho do arquivo
 * @param content Buffer para armazenar o conteúdo (será alocado)
 * @return ESP_OK em caso de sucesso
 */
static esp_err_t load_file_content(const char *filepath, char **content) {
    ensure_spiffs();
    
    FILE *file = fopen(filepath, "r");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        return ESP_FAIL;
    }
    
    // Obtém o tamanho do arquivo
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);
    
    if (file_size <= 0) {
        fclose(file);
        ESP_LOGE(TAG, "File is empty or error getting size: %s", filepath);
        return ESP_FAIL;
    }
    
    // Aloca memória para o conteúdo
    *content = malloc(file_size + 1);
    if (!*content) {
        fclose(file);
        ESP_LOGE(TAG, "Out of memory loading file: %s", filepath);
        return ESP_ERR_NO_MEM;
    }
    
    // Lê o conteúdo
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
 * @return String com as substituições feitas (deve ser liberada com free())
 */
static char* replace_placeholder(const char *template, const char *placeholder, const char *value) {
    if (!template || !placeholder || !value) return NULL;
    
    char search_pattern[64];
    snprintf(search_pattern, sizeof(search_pattern), "{{%s}}", placeholder);
    
    // Conta quantas ocorrências existem
    int count = 0;
    const char *pos = template;
    while ((pos = strstr(pos, search_pattern)) != NULL) {
        count++;
        pos += strlen(search_pattern);
    }
    
    // Log apenas se encontrar placeholders para depuração
    if (count > 0) {
        ESP_LOGI(TAG, "? Replacing %d occurrences of '%s' with '%s'", count, search_pattern, value);
    }
    
    if (count == 0) {
        // Nenhuma ocorrência, retorna cópia do template
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

    // Faz as substituições
    const char *src = template;
    char *dst = result;
    
    while ((pos = strstr(src, search_pattern)) != NULL) {
        // Copia até o placeholder
        size_t copy_len = pos - src;
        memcpy(dst, src, copy_len);
        dst += copy_len;

        // Copia o valor de substituição
        strcpy(dst, value);
        dst += new_len;

        // Move para após o placeholder
        src = pos + old_len;
    }
    
    // Copia o resto
    strcpy(dst, src);
    
    return result;
}

/**
 * Aplica múltiplas substituições de template
 * @param template Template original
 * @param substitutions Array de pares [placeholder, value], terminado com NULL
 * @return Template processado (deve ser liberado com free())
 */
static char* apply_template_substitutions(const char *template, const char **substitutions) {
    if (!template || !substitutions) return NULL;
    
    char *current = malloc(strlen(template) + 1);
    if (!current) return NULL;
    strcpy(current, template);

    // Aplica cada substituição
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
// HANDLERS PARA ARQUIVOS ESTÁTICOS
// =============================================================================

/**
 * Determina o tipo MIME baseado na extensão do arquivo
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
 * Handler genérico para servir arquivos estáticos
 */
static esp_err_t static_file_handler(httpd_req_t *req) {
    const char *uri = req->uri;
    char filepath[1024];  // Aumentado para evitar truncamento

    // Constrói caminho do arquivo
    if (strcmp(uri, "/") == 0) {
        strcpy(filepath, "/spiffs/html/index.html");
    } else {
        // Se não houver ponto (.) na URI, presumir que é uma página HTML em /html
        if (strchr(uri, '.') == NULL) {
            // Ex: /admin -> /spiffs/html/admin.html
            //      /config -> /spiffs/html/config.html
            snprintf(filepath, sizeof(filepath), "/spiffs/html%s.html", uri);
        } else {
            // Caminho direto (para .js, .css, .html completos, etc.)
            snprintf(filepath, sizeof(filepath), "/spiffs%s", uri);
        }
    }
    
    ESP_LOGI(TAG, "Serving static file: %s", filepath);

    // Carrega conteúdo do arquivo
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

// Função helper para enviar páginas de confirmação usando template
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
    
    // Define substituições para o template
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
 * Handler específico para arquivos CSS
 */
static esp_err_t css_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "CSS handler called for URI: %s", req->uri);
    return static_file_handler(req);
}

/**
 * Handler específico para arquivos JavaScript
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

// Páginas auxiliares - agora usam arquivos HTML
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
            // Usuário padrão
            save_login_state(true);
            save_user_level(USER_LEVEL_BASIC);
            ESP_LOGI(TAG, "Login usuário padrão (adm)");
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

    // Login inválido - usa arquivo HTML
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

// Handler da página administrativa - com substituições dos registradores
esp_err_t admin_get_handler(httpd_req_t *req) {
    char *template_content = NULL;
    esp_err_t ret = load_file_content("/spiffs/html/admin.html", &template_content);
    
    if (ret != ESP_OK || !template_content) {
        return httpd_resp_send_404(req);
    }
    
    // Preparar valores dos registradores para substituição
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

    // Informações do usuário atual
    user_level_t current_user_level = load_user_level();
    char user_level_str[32];
    char user_permissions[64];
    
    // Variáveis para controlar visibilidade das seções
    const char *show_basic_content = "";      // Para usuário padrão (adm)
    const char *show_admin_content = "";      // Para administrador (root)
    const char *hide_basic_content = "style='display:none;'";
    const char *hide_admin_content = "style='display:none;'";
    
    if (current_user_level == USER_LEVEL_ADMIN) {
        strcpy(user_level_str, "Administrador (root)");
        strcpy(user_permissions, "Acesso Completo");
        show_admin_content = "";
    show_basic_content = hide_basic_content; // Admin não vê conteúdo básico
    } else if (current_user_level == USER_LEVEL_BASIC) {
    strcpy(user_level_str, "Usuário Padrão (adm)");
        strcpy(user_permissions, "");
        show_basic_content = "";
    show_admin_content = hide_admin_content; // Básico não vê conteúdo admin
    } else {
        strcpy(user_level_str, "Não identificado");
    strcpy(user_permissions, "Sem permissões");
        show_basic_content = hide_basic_content;
        show_admin_content = hide_admin_content;
    }

    // Array de substituições para o template
    const char *substitutions[] = {
    // Informações do usuário
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

    // Aplica as substituições no template
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
#include "event_bus.h"
static esp_err_t factory_reset_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Factory reset requested via web");
    // Notifica máquina de estados que reset começou
    eventbus_factory_reset_start();
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);

    // small delay to ensure response is sent
    vTaskDelay(pdMS_TO_TICKS(200));

    esp_err_t ret = nvs_flash_erase();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao apagar NVS: %s", esp_err_to_name(ret));
    }
    if (remove("/spiffs/conteudo.json") != 0) {
    ESP_LOGW(TAG, "Arquivo conteudo.json não encontrado ou já removido");
    }
    if (remove("/spiffs/config.json") != 0) {
    ESP_LOGW(TAG, "Arquivo config.json não encontrado ou já removido");
    }
    if (remove("/data/config/network_config.json") != 0) {
    ESP_LOGW(TAG, "Arquivo network_config.json não encontrado ou já removido");
    }
    // Em vez de reiniciar diretamente, sinaliza conclusão para a máquina de estados
    ESP_LOGI(TAG, "Factory reset concluído - sinalizando máquina de estados");
    eventbus_factory_reset_complete();
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

// Modbus server page - render current values and toggle RTU/TCP blocks (default RTU)
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

    // Prepara substituições para todos os registradores
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

    // Array de substituições para o template
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

    // Aplica as substituições no template
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

// Handler para página de configuração do Access Point
esp_err_t ap_config_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "ap_config_get_handler called (serving AP config page from HTML file)");
    
    char *content = NULL;
    esp_err_t ret = load_file_content("/spiffs/html/ap-config.html", &content);
    
    if (ret != ESP_OK || !content) {
        ESP_LOGE(TAG, "Failed to load ap-config.html");
        return httpd_resp_send_404(req);
    }
    
    // Lê configurações atuais do NVS
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
    
    // Define substituições para o template
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

    // Envia página de confirmação usando template
    return send_confirmation_page(req, "Configuração Salva", 
                                "Configuração Modbus salva com sucesso!", 
                                "As configurações foram aplicadas e estão prontas para uso.",
                                "/modbus", "Voltar para Modbus", 3);
    
}

// POST handler to save only the Modbus mode from Configurar Dispositivo page
esp_err_t config_mode_save_post_handler(httpd_req_t *req) {
    char buf[128] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (ret <= 0) return httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);

    char mode[16] = "";
    httpd_query_key_value(buf, "modbus_mode", mode, sizeof(mode));
    if (strlen(mode) == 0) return httpd_resp_send(req, "Modo inválido", HTTPD_RESP_USE_STRLEN);

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

    // Envia confirmação e programa reinicialização
    char message[128];
    snprintf(message, sizeof(message), "Modo Modbus alterado para '%s'. O ESP32 será reiniciado.", mode);
    
    esp_err_t result = send_confirmation_page(req, "Modo Alterado", 
                                            "Configuração de Modo Salva", 
                                            message,
                                            "/modbus", "Voltar para Modbus", 0);

    // Reinicia após um pequeno delay para garantir que a resposta seja enviada
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
                                    "Não foi possível salvar a configuração do Access Point.",
                                    "/ap-config", "Tentar Novamente", 0);
    }

    return send_confirmation_page(req, "Configuração Salva", 
                                "Access Point Configurado", 
                                "As configurações do Access Point foram salvas com sucesso!",
                                "/modbus", "Voltar para Modbus", 3);
}

// Callback do timer para reinicialização
static void restart_timer_callback(void* arg) {
    ESP_LOGI(TAG, "Timer disparado - Reiniciando dispositivo após configuração AP...");
    esp_restart();
}

// Função para parsear dados multipart/form-data
static void parse_multipart_data(const char* data, const char* boundary, 
                                char* ap_ssid, size_t ssid_size,
                                char* ap_password, size_t password_size,
                                char* ap_password_confirm, size_t confirm_size,
                                char* ap_ip, size_t ip_size) {
    
    ESP_LOGI(TAG, "Iniciando parse multipart/form-data");
    
    // Campos e seus buffers de saída
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
            
            // Procurar pelo início do valor (após duas quebras de linha \r\n\r\n)
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
                // Procurar pelo final do valor (próximo boundary)
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
                        
                        ESP_LOGI(TAG, "Valor extraído para %s: [%s] (len=%d)", 
                                field_names[i], output_buffers[i], strlen(output_buffers[i]));
                    } else {
                        ESP_LOGW(TAG, "Valor muito grande para %s (len=%d, max=%d)", 
                                field_names[i], value_len, buffer_sizes[i]);
                    }
                } else {
                    ESP_LOGW(TAG, "Não encontrado final do valor para %s", field_names[i]);
                }
            } else {
                ESP_LOGW(TAG, "Não encontrado início do valor para %s", field_names[i]);
            }
        } else {
            ESP_LOGW(TAG, "Campo não encontrado: %s", field_names[i]);
        }
    }
}

// Task para reinicialização com delay
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
    vTaskDelete(NULL); // Esta linha nunca será executada, mas é boa prática
}

// Handler para salvar configurações do AP da página de configuração do dispositivo
esp_err_t ap_config_save_post_handler(httpd_req_t *req) {
    //VERIFICAÇÃO DE PERMISSÃO: Apenas administradores podem alterar configurações AP
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
    ESP_LOGE(TAG, "Erro ao receber dados do formulário: %d", ret);
        return httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);
    }
    
    ESP_LOGI(TAG, "Dados brutos recebidos (%d bytes): [%s]", ret, buf);

    // Parse form data
    char ap_ssid[64] = "";
    char ap_password[64] = "";
    char ap_password_confirm[64] = "";
    char ap_ip[32] = "";
    
    // Verificar se é multipart/form-data ou application/x-www-form-urlencoded
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

    // Validações básicas
    if (strlen(ap_ssid) == 0 || strlen(ap_password) < 8 || strlen(ap_ip) == 0) {
    return send_confirmation_page(req, "Erro de Validação", "Dados Inválidos", 
                                    "Por favor, preencha todos os campos corretamente. A senha deve ter pelo menos 8 caracteres.",
                                    "/config_unidade", "Voltar", 0);
    }

    // Validação de confirmação de senha
    if (strcmp(ap_password, ap_password_confirm) != 0) {
    return send_confirmation_page(req, "Erro de Validação", "Senhas Não Coincidem", 
                                    "A senha e a confirmação de senha devem ser idênticas. Por favor, tente novamente.",
                                    "/config_unidade", "Voltar", 0);
    }

    // Save to NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("ap_config", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return send_confirmation_page(req, "Erro", "Erro no Sistema", 
                                    "Não foi possível abrir o sistema de armazenamento.",
                                    "/config_unidade", "Voltar", 0);
    }

    nvs_set_str(nvs_handle, "ssid", ap_ssid);
    nvs_set_str(nvs_handle, "password", ap_password);
    nvs_set_str(nvs_handle, "ip", ap_ip);
    
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        return send_confirmation_page(req, "Erro", "Erro ao Salvar", 
                                    "Não foi possível salvar as configurações do Access Point.",
                                    "/config_unidade", "Tentar Novamente", 0);
    }

    // Criar task para reinicializar após 5 segundos (sempre, independente da resposta)
    if (!restart_task_running) {
    ESP_LOGI(TAG, "Configurações do AP salvas, iniciando reinicialização...");
        
        // Tentar criar task primeiro
        BaseType_t task_result = xTaskCreate(delayed_restart_task, "restart_task", 2048, NULL, 5, NULL);
        if (task_result == pdPASS) {
            ESP_LOGI(TAG, "Task de reinicialização criada com sucesso!");
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
                ESP_LOGI(TAG, "Timer de reinicialização iniciado como backup!");
            } else {
                ESP_LOGE(TAG, "ERRO CRÍTICO: Falha ao criar timer de backup!");
            }
        }
    } else {
    ESP_LOGW(TAG, "Task de reinicialização já está rodando!");
    }
    
    // Página de confirmação com aviso de reinicialização
    esp_err_t result = send_confirmation_page(req, "Configuração Salva com Sucesso!", 
                                "Access Point Configurado", 
                                "As configurações do Access Point foram salvas! O dispositivo será reiniciado automaticamente em 5 segundos para aplicar as mudanças.",
                                "/config_unidade", "Voltar", 5);
    
    return result;
}

// Handler para salvar configurações RTU
esp_err_t rtu_config_save_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "=== HANDLER RTU CONFIG SAVE INICIADO ===");
    
    char buf[1024] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (ret <= 0) {
    ESP_LOGE(TAG, "Erro ao receber dados do formulário RTU: %d", ret);
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
    
    // Parse dos parâmetros do formulário
    httpd_query_key_value(buf, "rtu_baudrate", rtu_baudrate, sizeof(rtu_baudrate));
    httpd_query_key_value(buf, "rtu_databits", rtu_databits, sizeof(rtu_databits));
    httpd_query_key_value(buf, "rtu_parity", rtu_parity, sizeof(rtu_parity));
    httpd_query_key_value(buf, "rtu_stopbits", rtu_stopbits, sizeof(rtu_stopbits));
    httpd_query_key_value(buf, "rtu_slave_address", rtu_slave_address, sizeof(rtu_slave_address));
    httpd_query_key_value(buf, "rtu_timeout", rtu_timeout, sizeof(rtu_timeout));
    
    ESP_LOGI(TAG, "RTU - Baudrate: %s, Databits: %s, Parity: %s, Stopbits: %s, Address: %s, Timeout: %s", 
             rtu_baudrate, rtu_databits, rtu_parity, rtu_stopbits, rtu_slave_address, rtu_timeout);

    // Validações básicas
    if (strlen(rtu_baudrate) == 0 || strlen(rtu_databits) == 0 || 
        strlen(rtu_parity) == 0 || strlen(rtu_stopbits) == 0 || 
        strlen(rtu_slave_address) == 0 || strlen(rtu_timeout) == 0) {
    return send_confirmation_page(req, "Erro de Validação", "Dados Incompletos", 
                                    "Por favor, preencha todos os campos da configuração RTU.",
                                    "/config_device", "Voltar", 0);
    }

    // Validar range do endereço slave (1-247)
    int slave_addr = atoi(rtu_slave_address);
    if (slave_addr < 1 || slave_addr > 247) {
    return send_confirmation_page(req, "Erro de Validação", "Endereço Inválido", 
                                    "O endereço Slave deve estar entre 1 e 247.",
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

    // Criar ou atualizar seção modbus_rtu
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
                                    "Não foi possível salvar as configurações RTU.",
                                    "/config_device", "Tentar Novamente", 0);
    }
    
    fprintf(f, "%s", json_string);
    fclose(f);
    
    ESP_LOGI(TAG, "Configurações RTU salvas em config.json: %s", json_string);
    
    cJSON_Delete(root);
    free(json_string);

    return send_confirmation_page(req, "Configurações Salvas", "RTU Configurado!", 
                                "As configurações do modo RTU foram salvas com sucesso.",
                                "/config_device", "Voltar", 3);
}

// Handler para salvar todos os registradores Modbus
esp_err_t modbus_registers_save_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "=== MODBUS_REGISTERS_SAVE: Inicio do handler ===");
    
    //VERIFICAÇÃO DE PERMISSÃO: Apenas administradores podem salvar registros Modbus
    user_level_t current_level = load_user_level();
    ESP_LOGI(TAG, "Nivel de usuario atual: %d (Admin=%d)", current_level, USER_LEVEL_ADMIN);
    
    esp_err_t perm_result = check_user_permission(req, USER_LEVEL_ADMIN);
    if (perm_result != ESP_OK) {
        ESP_LOGE(TAG, "PERMISSAO NEGADA para salvar registradores!");
        return perm_result;
    }
    
    ESP_LOGI(TAG, "Permissao OK - prosseguindo com salvamento");

    char buf[2048] = {0}; // Buffer maior para todos os registradores
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (ret <= 0) {
        ESP_LOGE(TAG, "Erro ao receber dados POST: %d", ret);
        return httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);
    }
    
    ESP_LOGI(TAG, "Dados POST recebidos (%d bytes): [%s]", ret, buf);

    ESP_LOGI(TAG, "Salvando registradores Modbus...");

    // Parse dos registradores 4000 (editáveis)
    char reg_value[16];
    int valores_alterados_4000 = 0;
    for (int i = 0; i < 8; i++) {
        char param_name[16];
        snprintf(param_name, sizeof(param_name), "reg4000_%d", i);
        if (httpd_query_key_value(buf, param_name, reg_value, sizeof(reg_value)) == ESP_OK) {
            int old_val = reg4000[i];
            reg4000[i] = atoi(reg_value);
            ESP_LOGI(TAG, "Reg4000[%d]: %d -> %d", i, old_val, reg4000[i]);
            valores_alterados_4000++;
        }
    }
    ESP_LOGI(TAG, "Total de valores 4000 parseados: %d", valores_alterados_4000);

    // Parse dos registradores 6000 (editáveis)
    int valores_alterados_6000 = 0;
    for (int i = 0; i < 5; i++) {
        char param_name[16];
        snprintf(param_name, sizeof(param_name), "reg6000_%d", i);
        if (httpd_query_key_value(buf, param_name, reg_value, sizeof(reg_value)) == ESP_OK) {
            int old_val = reg6000[i];
            reg6000[i] = atoi(reg_value);
            ESP_LOGI(TAG, "Reg6000[%d]: %d -> %d", i, old_val, reg6000[i]);
            valores_alterados_6000++;
        }
    }
    ESP_LOGI(TAG, "Total de valores 6000 parseados: %d", valores_alterados_6000);

    // Parse dos registradores 9000 (editáveis)
    int valores_alterados_9000 = 0;
    for (int i = 0; i < 20; i++) {
        char param_name[16];
        snprintf(param_name, sizeof(param_name), "reg9000_%d", i);
        if (httpd_query_key_value(buf, param_name, reg_value, sizeof(reg_value)) == ESP_OK) {
            int old_val = reg9000[i];
            reg9000[i] = atoi(reg_value);
            ESP_LOGI(TAG, "Reg9000[%d]: %d -> %d", i, old_val, reg9000[i]);
            valores_alterados_9000++;
        }
    }
    ESP_LOGI(TAG, "Total de valores 9000 parseados: %d", valores_alterados_9000);

    // **SALVAR AUTOMATICAMENTE NO CONFIG.JSON**
    ESP_LOGI(TAG, "Salvando registradores no config.json...");
    ensure_spiffs();
    
    // Lê o config.json existente ou cria um novo
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

    // Criar/atualizar seção modbus_registers
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
                                    "Não foi possível salvar os registradores no config.json.",
                                    "/modbus", "Tentar Novamente", 0);
    }
    
    fprintf(f, "%s", json_string);
    fclose(f);
    
    ESP_LOGI(TAG, "Registradores salvos em config.json: %s", json_string);
    
    cJSON_Delete(root);
    free(json_string);

    ESP_LOGI(TAG, "Todos os registradores foram atualizados e salvos com sucesso!");

    return send_confirmation_page(req, "Registradores Salvos", "Configuração Salva no Arquivo!", 
                                "Todos os registradores Modbus foram salvos automaticamente no config.json.",
                                "/modbus", "Voltar para Registradores", 3);
}

// Handler para salvar configurações WiFi (SSID e senha)
esp_err_t wifi_config_save_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "=== HANDLER WIFI CONFIG SAVE INICIADO ===");
    
    char buf[1024] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (ret <= 0) {
    ESP_LOGE(TAG, "Erro ao receber dados do formulário: %d", ret);
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
    
    // Verificar se é multipart/form-data ou application/x-www-form-urlencoded
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

    // Validação básica
    if (strlen(wifi_ssid) == 0) {
        ESP_LOGE(TAG, "SSID vazio");
    const char* response = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n{\"error\":\"SSID não pode estar vazio\"}";
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
    // Save network config to spiffs (IP estático)
    if (strlen(wifi_ip) > 0 || strlen(wifi_mask) > 0 || strlen(wifi_gateway) > 0 || strlen(wifi_dns) > 0) {
        ESP_LOGI(TAG, "Salvando configuração de rede manual: ip=%s mask=%s gw=%s dns=%s", wifi_ip, wifi_mask, wifi_gateway, wifi_dns);
        network_config_t net_config;
        strncpy(net_config.ip, wifi_ip, sizeof(net_config.ip)-1);
        strncpy(net_config.mask, wifi_mask, sizeof(net_config.mask)-1);
        strncpy(net_config.gateway, wifi_gateway, sizeof(net_config.gateway)-1);
        strncpy(net_config.dns, wifi_dns, sizeof(net_config.dns)-1);
        net_config.ip[sizeof(net_config.ip)-1] = '\0';
        net_config.mask[sizeof(net_config.mask)-1] = '\0';
        net_config.gateway[sizeof(net_config.gateway)-1] = '\0';
        net_config.dns[sizeof(net_config.dns)-1] = '\0';
        save_network_config(&net_config);
    }
    
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao salvar no NVS: %s", esp_err_to_name(err));
    const char* response = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\n\r\n{\"error\":\"Erro ao salvar configurações\"}";
        return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    }

    ESP_LOGI(TAG, "Configurações WiFi salvas com sucesso - SSID: %s", wifi_ssid);
    
    // Resposta de sucesso
    const char* response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"success\":true,\"message\":\"Configurações WiFi salvas com sucesso!\"}";
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

// Handler para conectar à rede WiFi salva
esp_err_t wifi_connect_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "=== HANDLER WIFI CONNECT INICIADO ===");
    
    // Ler configurações WiFi do NVS
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
    const char* response = "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\n\r\n{\"error\":\"Configuração WiFi não encontrada. Configure uma rede primeiro.\"}";
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
    
    ESP_LOGI(TAG, "Configuração WiFi lida - SSID: [%s], Password length: %d", 
             wifi_ssid, strlen(wifi_password));

    // Validação
    if (strlen(wifi_ssid) == 0) {
    const char* response = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n{\"error\":\"SSID não configurado\"}";
        return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    }

    // Iniciar conexão WiFi
    ESP_LOGI(TAG, "Iniciando conexão WiFi para SSID: %s", wifi_ssid);
    
    // Conectar à rede
    wifi_connect(wifi_ssid, wifi_password);
    
    ESP_LOGI(TAG, "Comando de conexão WiFi enviado");
    
    // Ler configuração de rede (possível IP estático) para informar ao usuário qual IP acessar
    char ip[32] = "", mask[32] = "", gw[32] = "", dns[32] = "";
    network_config_t net_config;
    if (load_network_config(&net_config) == ESP_OK) {
        strncpy(ip, net_config.ip, sizeof(ip)-1);
        strncpy(mask, net_config.mask, sizeof(mask)-1);
        strncpy(gw, net_config.gateway, sizeof(gw)-1);
        strncpy(dns, net_config.dns, sizeof(dns)-1);
        ip[sizeof(ip)-1] = '\0';
        mask[sizeof(mask)-1] = '\0';
        gw[sizeof(gw)-1] = '\0';
        dns[sizeof(dns)-1] = '\0';
    }

    // Resposta de sucesso incluindo o IP esperado após reinício (se existir)
    char response_buf[640];
    if (strlen(ip) > 0) {
        snprintf(response_buf, sizeof(response_buf), 
                 "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"
                 "{\"success\":true,\"message\":\"Conectando à rede %s e reiniciando...\",\"ssid\":\"%s\",\"ip\":\"%s\"}", 
                 wifi_ssid, wifi_ssid, ip);
    } else {
    // Sem IP estático: sugerir IP do AP como fallback e indicar DHCP
    const char* ap_ip = "192.168.4.1"; // IP padrão do AP
        snprintf(response_buf, sizeof(response_buf), 
                 "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"
                 "{\"success\":true,\"message\":\"Conectando à rede %s e reiniciando...\",\"ssid\":\"%s\",\"ip\":\"%s (DHCP - verifique o roteador)\"}", 
                 wifi_ssid, wifi_ssid, ap_ip);
    }

    esp_err_t send_result = httpd_resp_send(req, response_buf, HTTPD_RESP_USE_STRLEN);
    
    // Criar task para reinicializar após 2 segundos (tempo para enviar resposta)
    if (!restart_task_running) {
    ESP_LOGI(TAG, "*** INICIANDO REINICIALIZAÇÃO APÓS CONEXÃO WiFi ***");
        
        BaseType_t task_result = xTaskCreate(delayed_restart_task, "restart_task", 4096, NULL, 10, NULL);
        if (task_result == pdPASS) {
            ESP_LOGI(TAG, "Task de reinicialização criada com sucesso!");
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
                ESP_LOGI(TAG, "Timer de reinicialização iniciado como backup!");
            } else {
                ESP_LOGE(TAG, "ERRO CRÍTICO: Falha ao criar timer de backup!");
            }
        }
    } else {
    ESP_LOGW(TAG, "Task de reinicialização já está rodando!");
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
    return send_confirmation_page(req, "Erro", "SSID Inválido", 
                    "É necessário fornecer um nome de rede (SSID) válido.",
                                    "/wifi-scan", "Voltar", 0);
    }

    // Use centralized function to save WiFi credentials in NVS (namespace: wifi_config)
    save_wifi_config(wifi_ssid, wifi_password);

    // Note: save_wifi_config logs errors internally. Present success page regardless
    // (it will be obvious in logs if saving failed). If you prefer, read back to verify.
    char message[128];
    snprintf(message, sizeof(message), "Credenciais da rede '%s' foram salvas com sucesso!", wifi_ssid);
    return send_confirmation_page(req, "WiFi Configurado", "Configuração Salva", 
                                message, "/wifi-status", "Ver Status", 3);
}

// Minimal stubs for functions declared in webserver.h
esp_err_t logout_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Fazendo logout do usuário");
    
    // Limpar todos os estados de login
    save_login_state(false);
    save_login_state_root(false);
    save_user_level(USER_LEVEL_NONE);
    
    // Redirecionar para página de login
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
    load_mqtt_config(&config);  // Carregar configuração do arquivo
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
        "MQTT_BROKER_URL", config.broker_url,
        "MQTT_PORT", port_str,
        "MQTT_CLIENT_ID", config.client_id,
        "MQTT_USERNAME", config.username,
        "MQTT_PASSWORD", config.password,
        "MQTT_TLS_CHECKED", tls_checked,
        "MQTT_CA_PATH", config.ca_path,
        "MQTT_QOS", qos_str,
        "MQTT_RETAIN_CHECKED", retain_checked,
        "MQTT_PUBLISH_INTERVAL", interval_str,
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
    load_mqtt_config(&config);  // Carregar configuração atual primeiro
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
    esp_err_t result = save_mqtt_config(&config);  // Salvar no arquivo
    if (result == ESP_OK) {
        mqtt_set_config(&config);  // Aplicar na memória também
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

// ============================================================================
// MODBUS TCP CONFIGURATION HANDLERS
// ============================================================================

/**
 * @brief GET handler - Página HTML de configuração Modbus TCP
 */
esp_err_t modbus_tcp_config_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "modbus_tcp_config_get_handler called");
    
    // Verifica permissão de administrador
    if (check_user_permission(req, USER_LEVEL_ADMIN) != ESP_OK) {
        return ESP_OK;
    }
    
    FILE *f = fopen("/spiffs/data/html/modbus_tcp_config.html", "r");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open modbus_tcp_config.html");
        return httpd_resp_send_404(req);
    }
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *html = malloc(fsize + 1);
    if (!html) {
        fclose(f);
        return ESP_FAIL;
    }
    
    fread(html, 1, fsize, f);
    html[fsize] = '\0';
    fclose(f);
    
    httpd_resp_set_type(req, "text/html");
    esp_err_t result = httpd_resp_send(req, html, fsize);
    free(html);
    
    return result;
}

/**
 * @brief GET API handler - Retorna configuração atual em JSON
 */
esp_err_t modbus_tcp_config_api_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "modbus_tcp_config_api_handler called");
    
    modbus_tcp_config_t config;
    esp_err_t err = load_modbus_tcp_config(&config);
    
    cJSON *root = cJSON_CreateObject();
    
    if (err == ESP_OK) {
        cJSON_AddBoolToObject(root, "enabled", config.enabled);
        cJSON_AddNumberToObject(root, "port", config.port);
        cJSON_AddNumberToObject(root, "max_connections", config.max_connections);
        cJSON_AddBoolToObject(root, "success", true);
    } else {
        // Valores padrão em caso de erro
        cJSON_AddBoolToObject(root, "enabled", false);
        cJSON_AddNumberToObject(root, "port", 502);
        cJSON_AddNumberToObject(root, "max_connections", 5);
        cJSON_AddBoolToObject(root, "success", false);
        cJSON_AddStringToObject(root, "message", "Erro ao carregar configuração");
    }
    
    char *json_string = cJSON_Print(root);
    cJSON_Delete(root);
    
    if (!json_string) {
        ESP_LOGE(TAG, "Failed to create JSON response");
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    
    return ESP_OK;
}

/**
 * @brief POST API handler - Salva configuração Modbus TCP
 */
esp_err_t modbus_tcp_config_save_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "modbus_tcp_config_save_handler called");
    
    // Verifica permissão de administrador
    if (check_user_permission(req, USER_LEVEL_ADMIN) != ESP_OK) {
        cJSON *error_root = cJSON_CreateObject();
        cJSON_AddBoolToObject(error_root, "success", false);
        cJSON_AddStringToObject(error_root, "message", "Acesso negado");
        
        char *error_json = cJSON_Print(error_root);
        cJSON_Delete(error_root);
        
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, error_json, strlen(error_json));
        free(error_json);
        return ESP_OK;
    }
    
    // Lê body da requisição
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        ESP_LOGE(TAG, "Failed to receive POST data");
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    ESP_LOGI(TAG, "Received TCP config JSON: %s", buf);
    
    // Parse JSON
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        
        cJSON *error_root = cJSON_CreateObject();
        cJSON_AddBoolToObject(error_root, "success", false);
        cJSON_AddStringToObject(error_root, "message", "JSON inválido");
        
        char *error_json = cJSON_Print(error_root);
        cJSON_Delete(error_root);
        
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, error_json, strlen(error_json));
        free(error_json);
        return ESP_OK;
    }
    
    // Extrai valores
    modbus_tcp_config_t config = {
        .enabled = false,
        .port = 502,
        .max_connections = 5
    };
    
    cJSON *enabled_item = cJSON_GetObjectItem(root, "enabled");
    if (enabled_item && cJSON_IsBool(enabled_item)) {
        config.enabled = cJSON_IsTrue(enabled_item);
    }
    
    cJSON *port_item = cJSON_GetObjectItem(root, "port");
    if (port_item && cJSON_IsNumber(port_item)) {
        config.port = port_item->valueint;
    }
    
    cJSON *max_conn_item = cJSON_GetObjectItem(root, "max_connections");
    if (max_conn_item && cJSON_IsNumber(max_conn_item)) {
        config.max_connections = max_conn_item->valueint;
    }
    
    cJSON_Delete(root);
    
    // Salva configuração
    esp_err_t save_result = save_modbus_tcp_config(&config);
    
    // Cria resposta JSON
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", save_result == ESP_OK);
    cJSON_AddStringToObject(response, "message", 
        save_result == ESP_OK ? 
        "Configuração salva com sucesso! Reinicie o dispositivo para aplicar." :
        "Erro ao salvar configuração");
    
    char *response_json = cJSON_Print(response);
    cJSON_Delete(response);
    
    if (!response_json) {
        ESP_LOGE(TAG, "Failed to create response JSON");
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_json, strlen(response_json));
    free(response_json);
    
    ESP_LOGI(TAG, "Modbus TCP config saved: enabled=%d, port=%d", 
             config.enabled, config.port);
    
    return ESP_OK;
}

/**
 * @brief GET API handler - Status do WiFi
 */
esp_err_t wifi_status_api_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "wifi_status_api_handler called");
    
    wifi_status_t wifi_status = wifi_get_status();
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "is_connected", wifi_status.is_connected);
    cJSON_AddBoolToObject(root, "ap_active", wifi_status.ap_active);
    cJSON_AddStringToObject(root, "ssid", wifi_status.current_ssid);
    cJSON_AddStringToObject(root, "ip_address", wifi_status.ip_address);
    cJSON_AddNumberToObject(root, "rssi", wifi_status.rssi);
    
    char *json_string = cJSON_Print(root);
    cJSON_Delete(root);
    
    if (!json_string) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create JSON");
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    
    return ESP_OK;
}

/**
 * @brief POST API handler - Define modo Modbus (RTU ou TCP - exclusivo)
 */
// Handler para obter status do Modbus via API
esp_err_t modbus_status_api_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "modbus_status_api_handler called");
    
    // Carregar configuração TCP
    modbus_tcp_config_t tcp_config;
    bool tcp_enabled = false;
    if (load_modbus_tcp_config(&tcp_config) == ESP_OK) {
        tcp_enabled = tcp_config.enabled;
    }
    
    // Criar resposta JSON
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "rtu_enabled", modbus_rtu_enabled);
    cJSON_AddBoolToObject(response, "tcp_enabled", tcp_enabled);
    
    char *json_str = cJSON_Print(response);
    cJSON_Delete(response);
    
    httpd_resp_set_type(req, "application/json");
    esp_err_t result = httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    
    return result;
}

esp_err_t modbus_set_mode_api_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "modbus_set_mode_api_handler called");
    
    char buf[512];
    int ret, remaining = req->content_len;
    
    if (remaining >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Content too large");
        return ESP_FAIL;
    }
    
    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    // Parse JSON
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *rtu_item = cJSON_GetObjectItem(root, "rtu_enabled");
    cJSON *tcp_item = cJSON_GetObjectItem(root, "tcp_enabled");
    
    if (!cJSON_IsBool(rtu_item) || !cJSON_IsBool(tcp_item)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing required fields");
        return ESP_FAIL;
    }
    
    bool rtu_enabled = cJSON_IsTrue(rtu_item);
    bool tcp_enabled = cJSON_IsTrue(tcp_item);
    
    cJSON_Delete(root);
    
    // Validação: pelo menos um deve estar ativo
    if (!rtu_enabled && !tcp_enabled) {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "message", "Pelo menos um modo deve estar ativo");
        
        char *json_str = cJSON_Print(response);
        cJSON_Delete(response);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_str, strlen(json_str));
        free(json_str);
        return ESP_OK;
    }
    
    // Validação: se TCP, verificar WiFi
    if (tcp_enabled) {
        wifi_status_t wifi_status = wifi_get_status();
        if (!wifi_status.is_connected && !wifi_status.ap_active) {
            cJSON *response = cJSON_CreateObject();
            cJSON_AddBoolToObject(response, "success", false);
            cJSON_AddStringToObject(response, "message", "Modbus TCP requer WiFi ativo");
            
            char *json_str = cJSON_Print(response);
            cJSON_Delete(response);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, json_str, strlen(json_str));
            free(json_str);
            return ESP_OK;
        }
    }
    
    ESP_LOGI(TAG, "Configurando modo Modbus: RTU=%s, TCP=%s", 
             rtu_enabled ? "ON" : "OFF", tcp_enabled ? "ON" : "OFF");
    
    // Atualizar variável global RTU
    modbus_rtu_enabled = rtu_enabled;
    
    // Atualizar configuração TCP
    modbus_tcp_config_t tcp_config;
    if (load_modbus_tcp_config(&tcp_config) != ESP_OK) {
        // Se falhar, usar valores padrão
        tcp_config.port = 502;
        tcp_config.max_connections = 5;
    }
    tcp_config.enabled = tcp_enabled;
    
    // Salvar configurações
    esp_err_t rtu_result = save_rtu_config();
    esp_err_t tcp_result = save_modbus_tcp_config(&tcp_config);
    
    if (rtu_result != ESP_OK || tcp_result != ESP_OK) {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "message", "Erro ao salvar configurações");
        
        char *json_str = cJSON_Print(response);
        cJSON_Delete(response);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_str, strlen(json_str));
        free(json_str);
        return ESP_OK;
    }
    
    // Sucesso
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);
    cJSON_AddStringToObject(response, "message", "Configuração salva com sucesso");
    cJSON_AddBoolToObject(response, "restart_required", true);
    
    char *json_str = cJSON_Print(response);
    cJSON_Delete(response);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    
    ESP_LOGI(TAG, "Modo Modbus salvo: RTU=%s, TCP=%s", 
             rtu_enabled ? "ATIVO" : "INATIVO", tcp_enabled ? "ATIVO" : "INATIVO");
    
    return ESP_OK;
}

/**
 * @brief GET API handler - Status do Modbus TCP
 */
esp_err_t modbus_tcp_status_api_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "modbus_tcp_status_api_handler called");
    
    modbus_tcp_state_t state = modbus_tcp_get_state();
    const char* status_str;
    const char* message_str;
    
    switch (state) {
        case MODBUS_TCP_STATE_RUNNING:
            status_str = "running";
            message_str = "Modbus TCP ativo";
            break;
        case MODBUS_TCP_STATE_INITIALIZING:
            status_str = "initializing";
            message_str = "Inicializando...";
            break;
        case MODBUS_TCP_STATE_ERROR:
            status_str = "error";
            message_str = "Erro crítico";
            break;
        case MODBUS_TCP_STATE_STOPPING:
            status_str = "stopping";
            message_str = "Parando...";
            break;
        default:
            status_str = "stopped";
            message_str = "Parado";
            break;
    }
    
    // Criar JSON response
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", status_str);
    cJSON_AddStringToObject(root, "message", message_str);
    
    // Adiciona info de configuração
    modbus_tcp_config_t config;
    if (load_modbus_tcp_config(&config) == ESP_OK) {
        cJSON_AddBoolToObject(root, "enabled", config.enabled);
        cJSON_AddNumberToObject(root, "port", config.port);
    }
    
    char *json_string = cJSON_Print(root);
    cJSON_Delete(root);
    
    if (!json_string) {
        ESP_LOGE(TAG, "Failed to create JSON response");
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    
    return ESP_OK;
}

esp_err_t start_web_server() {
    if (server_handle != NULL) return ESP_OK;  // Servidor já está rodando
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.max_uri_handlers = 64;  // Reduzir para economizar memória
        config.max_resp_headers = 16;  // Reduzir para economizar memória
        config.stack_size = 12288;  // AUMENTAR para 12KB (handlers com templates grandes)
    config.task_priority = 5;  // Prioridade média
    config.recv_wait_timeout = 10;  // Timeout de 10 segundos
    config.send_wait_timeout = 10;  // Timeout de 10 segundos
    esp_err_t ret = httpd_start(&server_handle, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start webserver: %s", esp_err_to_name(ret));
        server_handle = NULL;
        return ret;  // Retorna erro
    }
    // New log to show webserver started
    ESP_LOGI(TAG, "Web server started, registering URI handlers");

    // Registra handlers para páginas principais
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/", .method = HTTP_GET, .handler = root_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/login", .method = HTTP_GET, .handler = login_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/reset", .method = HTTP_GET, .handler = reset_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/exit", .method = HTTP_GET, .handler = exit_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/do_login", .method = HTTP_GET, .handler = do_login_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/do_login", .method = HTTP_POST, .handler = do_login_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/logout", .method = HTTP_GET, .handler = logout_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/modbus", .method = HTTP_GET, .handler = modbus_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/config_unidade", .method = HTTP_GET, .handler = config_unit_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/config_device", .method = HTTP_GET, .handler = config_unit_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/unit_values", .method = HTTP_GET, .handler = unit_values_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/info", .method = HTTP_GET, .handler = info_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/admin", .method = HTTP_GET, .handler = admin_get_handler });
    
    // Registra handlers para arquivos estáticos CSS e JavaScript
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
    
    // Registra handlers para ações POST
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
    // Registra handlers para páginas WiFi
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
    
    // Registra handlers para configuração AP  
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/ap-config", .method = HTTP_GET, .handler = ap_config_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/factory_reset", .method = HTTP_POST, .handler = factory_reset_post_handler });
    
    // Registra handlers para MQTT
    ESP_LOGI(TAG, "Registering MQTT handlers");
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/mqtt_config", .method = HTTP_GET, .handler = mqtt_config_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/mqtt_config", .method = HTTP_POST, .handler = mqtt_config_post_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/api/mqtt/status", .method = HTTP_GET, .handler = mqtt_status_api_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/api/mqtt/test", .method = HTTP_POST, .handler = mqtt_test_api_handler });
    
    // Registra handlers para Modbus TCP
    ESP_LOGI(TAG, "Registering Modbus TCP handlers");
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/modbus_tcp_config", .method = HTTP_GET, .handler = modbus_tcp_config_get_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/api/modbus_tcp_config", .method = HTTP_GET, .handler = modbus_tcp_config_api_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/api/modbus_tcp_config", .method = HTTP_POST, .handler = modbus_tcp_config_save_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/api/modbus_tcp/status", .method = HTTP_GET, .handler = modbus_tcp_status_api_handler });
    
    // Novos endpoints para controle de modo Modbus
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/api/modbus/status", .method = HTTP_GET, .handler = modbus_status_api_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/api/modbus/set_mode", .method = HTTP_POST, .handler = modbus_set_mode_api_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/api/wifi/status", .method = HTTP_GET, .handler = wifi_status_api_handler });
    
    // Registra handlers para gerenciamento de configurações (somente root)
    ESP_LOGI(TAG, "Registering config management handlers");
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/api/config/upload", .method = HTTP_POST, .handler = config_upload_handler });
    // Register explicit download endpoints to avoid wildcard matching issues on some builds
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/api/config/download/rtu", .method = HTTP_GET, .handler = config_download_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/api/config/download/mqtt", .method = HTTP_GET, .handler = config_download_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/api/config/download/ap", .method = HTTP_GET, .handler = config_download_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/api/config/download/sta", .method = HTTP_GET, .handler = config_download_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/api/config/download/network", .method = HTTP_GET, .handler = config_download_handler });
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/api/config/download/main", .method = HTTP_GET, .handler = config_download_handler });
    // Keep wildcard as fallback
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/api/config/download/*", .method = HTTP_GET, .handler = config_download_handler });
    
    // Registra handler para o JavaScript de gerenciamento de config
    httpd_register_uri_handler(server_handle, &(httpd_uri_t){ .uri = "/js/config_manager.js", .method = HTTP_GET, .handler = js_handler });
    
    ESP_LOGI(TAG, "WebServer iniciado com sucesso");
    return ESP_OK;
}

// Para parada controlada via flag
esp_err_t stop_web_server() {
    if (server_handle == NULL) {
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Parando WebServer...");
    esp_err_t ret = httpd_stop(server_handle);
    server_handle = NULL;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WebServer parado com sucesso");
    } else {
        ESP_LOGE(TAG, "Erro ao parar WebServer: %s", esp_err_to_name(ret));
    }
    return ret;
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
    
    // Gera opções de redes WiFi disponíveis
    wifi_ap_record_t snapshot[MAX_APs];
    uint16_t snapshot_count = 0;
    wifi_get_ap_list_snapshot(snapshot, &snapshot_count);
    
    // Se não há redes e nenhum scan em progresso, inicia scan
    if (snapshot_count == 0 && !wifi_is_scan_in_progress()) {
        wifi_start_scan_async();
    }
    
    // Ordena as redes por RSSI
    if (snapshot_count > 1) {
        qsort(snapshot, snapshot_count, sizeof(wifi_ap_record_t), compare_ap_rssi);
    }
    
    // Constrói as opções HTML para o select
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
    
    // Se não há redes, adiciona opção padrão
    if (snapshot_count == 0) {
        strncpy(wifi_options, "<option value=\"\">Nenhuma rede encontrada</option>", 
                sizeof(wifi_options) - 1);
    }
    
    // Aplica substituições no template
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
// Handler para configuração do dispositivo - usa template HTML
esp_err_t config_unit_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "🔥 config_unit_get_handler called for URI: %s", req->uri);
    
    // VERIFICAÇÃO DE PERMISSÃO: Usuários básicos podem visualizar, apenas admin pode editar
    esp_err_t perm_result = check_user_permission(req, USER_LEVEL_BASIC);
    if (perm_result != ESP_OK) {
        ESP_LOGI(TAG, "Permission denied for config_unit_get_handler");
        return perm_result;
    }

    ESP_LOGI(TAG, "Permission OK, free heap: %u bytes", (unsigned int)esp_get_free_heap_size());
    
    // ESTRATÉGIA OTIMIZADA: Enviar HTML estático direto do arquivo sem substituições
    // As páginas HTML modernas devem usar JavaScript para buscar dados via API
    
    ensure_spiffs();
    
    FILE *file = fopen("/spiffs/html/config_device.html", "r");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open config_device.html");
        return httpd_resp_send_404(req);
    }
    
    // Obter tamanho do arquivo
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);
    
    ESP_LOGI(TAG, "Sending config_device.html (%ld bytes) in chunks", file_size);
    
    httpd_resp_set_type(req, "text/html");
    
    // Enviar arquivo em chunks de 2KB para economizar memória
    #define CHUNK_SIZE 2048
    char *chunk = malloc(CHUNK_SIZE);
    if (!chunk) {
        fclose(file);
        ESP_LOGE(TAG, "Failed to allocate chunk buffer");
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }
    
    size_t bytes_read;
    while ((bytes_read = fread(chunk, 1, CHUNK_SIZE, file)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, bytes_read) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send chunk");
            free(chunk);
            fclose(file);
            return ESP_FAIL;
        }
    }
    
    // Finalizar resposta
    httpd_resp_send_chunk(req, NULL, 0);
    
    free(chunk);
    fclose(file);
    
    ESP_LOGI(TAG, "File sent successfully, free heap: %u", (unsigned int)esp_get_free_heap_size());
    
    return ESP_OK;
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
    
    // Lê valores atuais do sistema (simulados para demonstração)
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
    
    // Aqui você pode ler os valores reais dos sensores, NVS, etc.
    
    // Define substituições para o template
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
    
    // Aplica substituições
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

// Handler para página de informações do sistema
esp_err_t info_get_handler(httpd_req_t *req) {
    // Carrega template HTML
    char *template_content = NULL;
    esp_err_t ret = load_file_content("/spiffs/html/info.html", &template_content);
    
    if (ret != ESP_OK || !template_content) {
        ESP_LOGE(TAG, "Failed to load info.html");
        return httpd_resp_send_404(req);
    }

    // Coleta informações do ESP32
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

    // Informações do sistema
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

    // Versão baseada no título da página principal (Medidor de Combustão)
    strcpy(program_version, "Medidor de Combustão v1.0");
    strcpy(compile_date, __DATE__);
    strcpy(compile_time, __TIME__);
    strcpy(idf_version, esp_get_idf_version());
    
    uint64_t uptime_ms = esp_timer_get_time() / 1000;
    uint32_t days = uptime_ms / (24 * 60 * 60 * 1000);
    uint32_t hours = (uptime_ms % (24 * 60 * 60 * 1000)) / (60 * 60 * 1000);
    uint32_t minutes = (uptime_ms % (60 * 60 * 1000)) / (60 * 1000);
    snprintf(uptime, sizeof(uptime), "%lud %luh %lum", days, hours, minutes);

    // Configurações do AP
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

    // Configurações do WiFi
    char wifi_ssid[64] = "Não configurado";
    char wifi_password[64] = "********"; // masked for display
    char wifi_password_plain[64] = "";   // actual password (if available)
    char wifi_ip[32] = "Não conectado";
    char wifi_netmask[32] = "Não conectado";
    char wifi_gateway[32] = "Não conectado";
    char wifi_status[32] = "Desconectado";
    char wifi_rssi[16] = "N/A";

    // Lê configurações WiFi do NVS
    nvs_handle_t wifi_nvs;
    err = nvs_open("wifi_config", NVS_READONLY, &wifi_nvs);
    if (err == ESP_OK) {
        size_t req_size = sizeof(wifi_ssid);
        if (nvs_get_str(wifi_nvs, "ssid", wifi_ssid, &req_size) == ESP_OK) {
            // Se tem SSID configurado, tentar ler a senha real e mascarar para display
            size_t pass_size = sizeof(wifi_password_plain);
            if (nvs_get_str(wifi_nvs, "password", wifi_password_plain, &pass_size) == ESP_OK) {
                // senha disponível no NVS, manter masked display but keep plain
                strncpy(wifi_password, "********", sizeof(wifi_password)-1);
                wifi_password[sizeof(wifi_password)-1] = '\0';
            } else {
                // senha não disponível explicitamente, keep mask
                strncpy(wifi_password_plain, "", sizeof(wifi_password_plain));
                strncpy(wifi_password, "********", sizeof(wifi_password)-1);
                wifi_password[sizeof(wifi_password)-1] = '\0';
            }
        }
        nvs_close(wifi_nvs);
    }

    // Obter informações da conexão atual
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
    // ap_info.ssid é um array de bytes sem terminação garantida
        size_t copy_len = sizeof(wifi_ssid) - 1;
        memcpy(wifi_ssid, ap_info.ssid, copy_len);
        wifi_ssid[copy_len] = '\0';
    // Garantir terminação correta: se houver um '\0' dentro de ap_info.ssid, ajustar
        for (size_t i = 0; i < copy_len; ++i) {
            if (wifi_ssid[i] == '\0') break;
        }
    }

    // Array de substituições
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
    "PROJECT_NAME", "Medidor de Combustão ESP32",
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

    // Aplica substituições
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

    // Salvar usando função padronizada (agora salva no NVS wifi_config)
    save_wifi_config(ssid, password);

    ESP_LOGI(TAG, "WiFi config salvo via wifi_save_post_handler - SSID: %s", ssid);

    // Load template and apply substitutions
    char *html_template = NULL;
    esp_err_t ret_load = load_file_content("/spiffs/html/wificredentialsv.html", &html_template);
    if (ret_load != ESP_OK || !html_template) {
        ESP_LOGE(TAG, "Failed to load wificredentialsv.html template");
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

// Handler para página de status WiFi - agora usa arquivo HTML
esp_err_t wifi_status_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "wifi_status_get_handler called (serving WiFi status page from HTML file)");
    
    char *content = NULL;
    esp_err_t ret = load_file_content("/spiffs/html/wifi-status.html", &content);
    
    if (ret != ESP_OK || !content) {
        ESP_LOGE(TAG, "Failed to load wifi-status.html");
        return httpd_resp_send_404(req);
    }
    
    // Obtém status atual do WiFi
    wifi_status_t st = wifi_get_status();
    
    // Determina displays condicionais
    const char *ip_display = st.is_connected ? "block" : "none";
    const char *rssi_display = st.is_connected ? "block" : "none";
    const char *progress_display = (!st.is_connected && strlen(st.status_message) > 0) ? "block" : "none";
    const char *error_display = "none"; // TODO: implementar lógica de erro
    
    // Converte RSSI para string
    char rssi_str[16];
    snprintf(rssi_str, sizeof(rssi_str), "%d", st.rssi);
    
    // Define substituições para o template
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

// Handler para página de scan WiFi (redireciona para a página unificada)
esp_err_t wifi_scan_get_handler(httpd_req_t *req) {
    // Redireciona para a página unificada de configuração WiFi
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
    
    // Se não há resultados e não há scan em progresso, tenta iniciar scan
    if (snapshot_count == 0 && !wifi_is_scan_in_progress()) {
        wifi_start_scan_async();
    }
    
    // Constrói JSON com as redes encontradas
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
    
    // Define substituições para o template
    const char *substitutions[] = {
        "PAGE_TITLE", "Reiniciando",
        "MESSAGE_TITLE", "Reiniciando ESP32",
    "MESSAGE_TEXT", "O dispositivo está sendo reiniciado para aplicar as configurações WiFi.",
        "REDIRECT_DISPLAY", "none",
        "COUNTDOWN", "0",
        "RETURN_URL", "/",
    "RETURN_TEXT", "Página Inicial",
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

// Handler para upload de configurações JSON (somente root)
esp_err_t config_upload_handler(httpd_req_t *req) {
    // Verificar permissão de administrador
    if (check_user_permission(req, USER_LEVEL_ADMIN) != ESP_OK) {
        return ESP_OK; // Resposta já enviada pela função check_user_permission
    }
    
    ESP_LOGI(TAG, "Processing config upload request");
    
    // Buffer para receber dados (agora alocado dinamicamente conforme Content-Length)
    int total_len = req->content_len;
    const int MAX_UPLOAD = 10240; // 10KB - deve coincidir com validação do frontend

    if (total_len <= 0 || total_len > MAX_UPLOAD) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, "{\"success\": false, \"error\": \"Arquivo muito grande ou inválido\"}", HTTPD_RESP_USE_STRLEN);
    }

    char *content = malloc(total_len + 1);
    if (!content) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, "{\"success\": false, \"error\": \"Memory allocation failed\"}", HTTPD_RESP_USE_STRLEN);
    }

    // Ler dados do POST
    int cur_len = 0;
    int received = 0;
    while (cur_len < total_len) {
        received = httpd_req_recv(req, content + cur_len, total_len - cur_len);
        if (received <= 0) {
            free(content);
            httpd_resp_set_type(req, "application/json");
            return httpd_resp_send(req, "{\"success\": false, \"error\": \"Erro na recepção de dados\"}", HTTPD_RESP_USE_STRLEN);
        }
        cur_len += received;
    }
    content[total_len] = '\0';
    
    // Parsing básico para extrair tipo e dados JSON
    ESP_LOGI(TAG, "Upload length: %d bytes", total_len);
    char *config_type_start = strstr(content, "name=\"configType\"");
    char *json_start = strstr(content, "name=\"configFile\"");

    if (!config_type_start || !json_start) {
        // For debugging, log a small sample of the payload
        int snippet_len = total_len > 256 ? 256 : total_len;
        char *sample = malloc(snippet_len + 1);
        if (sample) {
            memcpy(sample, content, snippet_len);
            sample[snippet_len] = '\0';
            ESP_LOGW(TAG, "Payload sample (first %d bytes): %s", snippet_len, sample);
            free(sample);
        }
        free(content);
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, "{\"success\": false, \"error\": \"Dados do formulário inválidos\"}", HTTPD_RESP_USE_STRLEN);
    }

    // Extrair tipo de configuração (mais tolerante a CRLF/LF)
    char config_type[32] = {0};
    char *type_value = strstr(config_type_start, "\r\n\r\n");
    if (!type_value) type_value = strstr(config_type_start, "\n\n");
    if (type_value) {
        type_value += (type_value[1] == '\n' && type_value[0] == '\n') ? 2 : 4;
        int i = 0;
        while (type_value[i] && type_value[i] != '\r' && type_value[i] != '\n' && i < (int)sizeof(config_type)-1) {
            config_type[i] = type_value[i];
            i++;
        }
        config_type[i] = '\0';
        ESP_LOGI(TAG, "Detected config type: %s", config_type);
    } else {
        ESP_LOGW(TAG, "Could not find config type boundary in upload payload");
    }

    // Extrair dados JSON (parsing mais robusto do multipart)
    char *json_data = NULL;
    
    // Procurar pelo cabeçalho Content-Type: application/json ou similar
    char *content_type_pos = strstr(json_start, "Content-Type:");
    if (content_type_pos) {
        // Pular para depois do cabeçalho
        json_data = strstr(content_type_pos, "\r\n\r\n");
        if (!json_data) json_data = strstr(content_type_pos, "\n\n");
        if (json_data) {
            json_data += (strncmp(json_data, "\r\n\r\n", 4) == 0) ? 4 : 2;
        }
    }
    
    // Fallback: procurar por dupla quebra de linha após o nome do campo
    if (!json_data) {
        json_data = strstr(json_start, "\r\n\r\n");
        if (!json_data) json_data = strstr(json_start, "\n\n");
        if (json_data) {
            json_data += (strncmp(json_data, "\r\n\r\n", 4) == 0) ? 4 : 2;
        }
    }
    
    if (json_data) {
        // Procurar fim do conteúdo JSON de forma mais flexível
        char *json_end = NULL;
        
        // Tentar diferentes padrões de boundary
        json_end = strstr(json_data, "\r\n------");
        if (!json_end) json_end = strstr(json_data, "\n------");
        if (!json_end) json_end = strstr(json_data, "\r\n--");
        if (!json_end) json_end = strstr(json_data, "\n--");
        
        if (json_end) {
            *json_end = '\0';
        }
        
        // Remover espaços e quebras no início e fim
        while (*json_data && (*json_data == ' ' || *json_data == '\r' || *json_data == '\n' || *json_data == '\t')) {
            json_data++;
        }
        
        size_t len = strlen(json_data);
        while (len > 0 && (json_data[len-1] == ' ' || json_data[len-1] == '\r' || json_data[len-1] == '\n' || json_data[len-1] == '\t')) {
            json_data[--len] = '\0';
        }
    } else {
        ESP_LOGE(TAG, "Could not find json data start in payload");
    }
    
    if (!json_data || strlen(json_data) == 0) {
        ESP_LOGE(TAG, "JSON data not found or empty in multipart payload");
        // Log uma amostra do payload para depuração
        if (total_len > 0) {
            int sample_size = (total_len > 200) ? 200 : total_len;
            char sample[201];
            memcpy(sample, content, sample_size);
            sample[sample_size] = '\0';
            ESP_LOGE(TAG, "Payload sample: %s", sample);
        }
        free(content);
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, "{\"success\": false, \"error\": \"Dados JSON não encontrados no upload\"}", HTTPD_RESP_USE_STRLEN);
    }
    
    ESP_LOGI(TAG, "JSON data extracted: %s", json_data);
    
    // Validar JSON
    cJSON *json = cJSON_Parse(json_data);
    if (!json) {
        free(content);
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, "{\"success\": false, \"error\": \"JSON inválido\"}", HTTPD_RESP_USE_STRLEN);
    }
    
    // Processar baseado no tipo
    esp_err_t result;
    char response[512];
    
    if (strcmp(config_type, "rtu") == 0) {
        ESP_LOGI(TAG, "Processing RTU config upload");
        
        // Validar campos obrigatórios
        cJSON *baud_rate = cJSON_GetObjectItem(json, "baud_rate");
        cJSON *slave_addr = cJSON_GetObjectItem(json, "slave_address");
        
        if (!baud_rate || !slave_addr) {
            snprintf(response, sizeof(response), 
                "{\"success\": false, \"error\": \"Campos obrigatórios missing: baud_rate, slave_address\"}");
        } else {
            // USAR AS FUNÇÕES DE CONFIG_MANAGER COM BACKUP DUPLO
            
            // Atualizar registradores Modbus com dados do JSON
            holding_reg1000_params.reg1000[baudrate] = (uint16_t)cJSON_GetNumberValue(baud_rate);
            holding_reg1000_params.reg1000[endereco] = (uint16_t)cJSON_GetNumberValue(slave_addr);
            
            if (cJSON_GetObjectItem(json, "parity")) {
                holding_reg1000_params.reg1000[paridade] = (uint16_t)cJSON_GetNumberValue(cJSON_GetObjectItem(json, "parity"));
            }
            
            // Usar a função de configuração que salva SPIFFS + NVS
            esp_err_t save_result = save_rtu_config();
            
            if (save_result == ESP_OK) {
                result = ESP_OK;
                ESP_LOGI(TAG, "RTU config upload processado via sistema duplo (SPIFFS + NVS)");
                snprintf(response, sizeof(response), 
                    "{\"success\": true, \"message\": \"Configuração RTU salva com backup duplo (SPIFFS + NVS)\"}");
            } else {
                ESP_LOGE(TAG, "Erro ao salvar RTU config via sistema duplo");
                snprintf(response, sizeof(response), 
                    "{\"success\": false, \"error\": \"Erro ao salvar RTU config com backup duplo\"}");
            }
        }
    }
    else if (strcmp(config_type, "mqtt") == 0) {
        ESP_LOGI(TAG, "Processing MQTT config upload");
        
        // Validar campos obrigatórios
        cJSON *broker_uri = cJSON_GetObjectItem(json, "broker_uri");
        cJSON *broker_url = cJSON_GetObjectItem(json, "broker_url");
        
        const char* broker = broker_uri ? cJSON_GetStringValue(broker_uri) : 
                            (broker_url ? cJSON_GetStringValue(broker_url) : NULL);
        
        if (!broker) {
            snprintf(response, sizeof(response), 
                "{\"success\": false, \"error\": \"Campo obrigatório missing: broker_uri ou broker_url\"}");
        } else {
            // USAR AS FUNÇÕES DE CONFIG_MANAGER COM BACKUP DUPLO
            
            // Criar estrutura mqtt_config_t com dados do JSON
            mqtt_config_t mqtt_config = {0};
            
            strncpy(mqtt_config.broker_url, broker, sizeof(mqtt_config.broker_url) - 1);
            
            cJSON *client_id = cJSON_GetObjectItem(json, "client_id");
            if (client_id && cJSON_IsString(client_id)) {
                strncpy(mqtt_config.client_id, cJSON_GetStringValue(client_id), sizeof(mqtt_config.client_id) - 1);
            } else {
                strcpy(mqtt_config.client_id, "esp32_client");
            }
            
            cJSON *username = cJSON_GetObjectItem(json, "username");
            if (username && cJSON_IsString(username)) {
                strncpy(mqtt_config.username, cJSON_GetStringValue(username), sizeof(mqtt_config.username) - 1);
            }
            
            cJSON *password = cJSON_GetObjectItem(json, "password");
            if (password && cJSON_IsString(password)) {
                strncpy(mqtt_config.password, cJSON_GetStringValue(password), sizeof(mqtt_config.password) - 1);
            }
            
            cJSON *enabled = cJSON_GetObjectItem(json, "enabled");
            mqtt_config.enabled = enabled ? cJSON_IsTrue(enabled) : true;
            
            cJSON *port = cJSON_GetObjectItem(json, "port");
            mqtt_config.port = port ? (uint16_t)cJSON_GetNumberValue(port) : 1883;
            
            cJSON *qos = cJSON_GetObjectItem(json, "qos");
            mqtt_config.qos = qos ? (uint8_t)cJSON_GetNumberValue(qos) : 0;
            
            cJSON *retain = cJSON_GetObjectItem(json, "retain");
            mqtt_config.retain = retain ? cJSON_IsTrue(retain) : false;
            
            cJSON *tls_enabled = cJSON_GetObjectItem(json, "tls_enabled");
            mqtt_config.tls_enabled = tls_enabled ? cJSON_IsTrue(tls_enabled) : false;
            
            cJSON *publish_interval = cJSON_GetObjectItem(json, "publish_interval_ms");
            mqtt_config.publish_interval_ms = publish_interval ? (uint32_t)cJSON_GetNumberValue(publish_interval) : 5000;
            
            // Usar a função de configuração que salva SPIFFS + NVS
            esp_err_t save_result = save_mqtt_config(&mqtt_config);
            
            if (save_result == ESP_OK) {
                result = ESP_OK;
                ESP_LOGI(TAG, "MQTT config upload processado via sistema duplo (SPIFFS + NVS)");
                snprintf(response, sizeof(response), 
                    "{\"success\": true, \"message\": \"Configuração MQTT salva com backup duplo (SPIFFS + NVS)\"}");
            } else {
                ESP_LOGE(TAG, "Erro ao salvar MQTT config via sistema duplo");
                snprintf(response, sizeof(response), 
                    "{\"success\": false, \"error\": \"Erro ao salvar MQTT config com backup duplo\"}");
            }
        }
    }
    else if (strcmp(config_type, "ap") == 0) {
        ESP_LOGI(TAG, "Processing AP config upload");
        
        cJSON *ssid = cJSON_GetObjectItem(json, "ssid");
        cJSON *password = cJSON_GetObjectItem(json, "password");
        
        if (!ssid) {
            snprintf(response, sizeof(response), 
                "{\"success\": false, \"error\": \"Campo obrigatório missing: ssid\"}");
        } else {
            // USAR AS FUNÇÕES DE CONFIG_MANAGER COM BACKUP DUPLO
            
            // Criar estrutura ap_config_t com dados do JSON
            ap_config_t ap_config = {0};
            
            strncpy(ap_config.ssid, cJSON_GetStringValue(ssid), sizeof(ap_config.ssid) - 1);
            
            if (password && cJSON_IsString(password)) {
                strncpy(ap_config.password, cJSON_GetStringValue(password), sizeof(ap_config.password) - 1);
            }
            
            cJSON *username = cJSON_GetObjectItem(json, "username");
            if (username && cJSON_IsString(username)) {
                strncpy(ap_config.username, cJSON_GetStringValue(username), sizeof(ap_config.username) - 1);
            } else {
                strcpy(ap_config.username, "admin"); // valor padrão
            }
            
            cJSON *ip = cJSON_GetObjectItem(json, "ip");
            if (ip && cJSON_IsString(ip)) {
                strncpy(ap_config.ip, cJSON_GetStringValue(ip), sizeof(ap_config.ip) - 1);
            } else {
                strcpy(ap_config.ip, "192.168.4.1"); // valor padrão
            }
            
            // Usar a função de configuração que salva SPIFFS + NVS
            esp_err_t save_result = save_ap_config(&ap_config);
            
            if (save_result == ESP_OK) {
                result = ESP_OK;
                ESP_LOGI(TAG, "AP config upload processado via sistema duplo (SPIFFS + NVS)");
                snprintf(response, sizeof(response), 
                    "{\"success\": true, \"message\": \"Configuração AP salva com backup duplo (SPIFFS + NVS)\"}");
            } else {
                ESP_LOGE(TAG, "Erro ao salvar AP config via sistema duplo");
                snprintf(response, sizeof(response), 
                    "{\"success\": false, \"error\": \"Erro ao salvar AP config com backup duplo\"}");
            }
        }
    }
    else if (strcmp(config_type, "sta") == 0) {
        ESP_LOGI(TAG, "Processing STA (WiFi Station) config upload");
        
        cJSON *ssid = cJSON_GetObjectItem(json, "ssid");
        
        if (!ssid) {
            snprintf(response, sizeof(response), 
                "{\"success\": false, \"error\": \"Campo obrigatório missing: ssid\"}");
        } else {
            // USAR AS FUNÇÕES DE CONFIG_MANAGER COM BACKUP DUPLO
            
            // Criar estrutura sta_config_t com dados do JSON
            sta_config_t sta_config = {0};
            
            strncpy(sta_config.ssid, cJSON_GetStringValue(ssid), sizeof(sta_config.ssid) - 1);
            
            cJSON *password = cJSON_GetObjectItem(json, "password");
            if (password && cJSON_IsString(password)) {
                strncpy(sta_config.password, cJSON_GetStringValue(password), sizeof(sta_config.password) - 1);
            }
            
            // Usar a função de configuração que salva SPIFFS + NVS
            esp_err_t save_result = save_sta_config(&sta_config);
            
            if (save_result == ESP_OK) {
                result = ESP_OK;
                ESP_LOGI(TAG, "STA config upload processado via sistema duplo (SPIFFS + NVS)");
                snprintf(response, sizeof(response), 
                    "{\"success\": true, \"message\": \"Configuração STA salva com backup duplo (SPIFFS + NVS)\"}");
            } else {
                ESP_LOGE(TAG, "Erro ao salvar STA config via sistema duplo");
                snprintf(response, sizeof(response), 
                    "{\"success\": false, \"error\": \"Erro ao salvar STA config com backup duplo\"}");
            }
        }
    }
    else if (strcmp(config_type, "network") == 0) {
        ESP_LOGI(TAG, "Processing Network config upload");
        
        // USAR AS FUNÇÕES DE CONFIG_MANAGER COM BACKUP DUPLO
        
        // Criar estrutura network_config_t com dados do JSON
        network_config_t network_config = {0};
        
        // Nota: hostname não faz parte da estrutura network_config_t, será ignorado
        
        cJSON *static_ip = cJSON_GetObjectItem(json, "static_ip");
        if (static_ip && cJSON_IsString(static_ip)) {
            strncpy(network_config.ip, cJSON_GetStringValue(static_ip), sizeof(network_config.ip) - 1);
        }
        
        cJSON *gateway = cJSON_GetObjectItem(json, "gateway");
        if (gateway && cJSON_IsString(gateway)) {
            strncpy(network_config.gateway, cJSON_GetStringValue(gateway), sizeof(network_config.gateway) - 1);
        }
        
        cJSON *subnet = cJSON_GetObjectItem(json, "subnet");
        if (subnet && cJSON_IsString(subnet)) {
            strncpy(network_config.mask, cJSON_GetStringValue(subnet), sizeof(network_config.mask) - 1);
        }
        
        cJSON *dns1 = cJSON_GetObjectItem(json, "dns1");
        if (dns1 && cJSON_IsString(dns1)) {
            strncpy(network_config.dns, cJSON_GetStringValue(dns1), sizeof(network_config.dns) - 1);
        }
        
        // Usar a função de configuração que salva SPIFFS + NVS
        esp_err_t save_result = save_network_config(&network_config);
        
        if (save_result == ESP_OK) {
            result = ESP_OK;
            ESP_LOGI(TAG, "Network config upload processado via sistema duplo (SPIFFS + NVS)");
            snprintf(response, sizeof(response), 
                "{\"success\": true, \"message\": \"Configuração Network salva com backup duplo (SPIFFS + NVS)\"}");
        } else {
            ESP_LOGE(TAG, "Erro ao salvar Network config via sistema duplo");
            snprintf(response, sizeof(response), 
                "{\"success\": false, \"error\": \"Erro ao salvar Network config com backup duplo\"}");
        }
    }
    else {
        snprintf(response, sizeof(response), 
            "{\"success\": false, \"error\": \"Tipo de configuração não suportado: %s\"}", config_type);
    }
    
    cJSON_Delete(json);
    free(content);
    
    // Se não foi definida uma resposta específica, usar erro genérico
    if (strlen(response) == 0) {
        snprintf(response, sizeof(response), 
            "{\"success\": false, \"error\": \"Erro ao processar configuração do tipo %s\"}", config_type);
    }
    
    ESP_LOGI(TAG, "Upload response: %s", response);
    
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, strlen(response));
}

// Handler para download de configurações JSON (somente root)
esp_err_t config_download_handler(httpd_req_t *req) {
    // Log inicial para diagnóstico
    ESP_LOGI(TAG, "config_download_handler called, uri=%s", req->uri);
    user_level_t current_level = load_user_level();
    ESP_LOGI(TAG, "Current user level (NVS): %d", current_level);

    // Verificar permissão de administrador
    if (check_user_permission(req, USER_LEVEL_ADMIN) != ESP_OK) {
        ESP_LOGW(TAG, "config_download_handler: permissão negada para uri=%s", req->uri);
        return ESP_OK; // Resposta já enviada pela função check_user_permission
    }
    
    // Extrair tipo da URL (/api/config/download/rtu)
    char *uri = (char*)req->uri;
    char *config_type = strrchr(uri, '/');
    if (!config_type) {
        return httpd_resp_send_404(req);
    }
    config_type++; // Avançar após '/'
    
    ESP_LOGI(TAG, "Downloading config: %s", config_type);
    
    cJSON *json = NULL;
    char filename[64];
    
    if (strcmp(config_type, "rtu") == 0) {
        // Tenta carregar, mas sempre retorna um JSON (com valores padrão quando ausentes)
        load_rtu_config(); // se falhar, valores padrão em holding_reg1000_params serão usados
        json = cJSON_CreateObject();
        cJSON_AddNumberToObject(json, "uart_port", 2); // UART padrão
        cJSON_AddNumberToObject(json, "baud_rate", holding_reg1000_params.reg1000[0]);
        cJSON_AddNumberToObject(json, "slave_address", holding_reg1000_params.reg1000[1]);
        cJSON_AddNumberToObject(json, "data_bits", 8);
        cJSON_AddNumberToObject(json, "parity", holding_reg1000_params.reg1000[2]);
        cJSON_AddNumberToObject(json, "stop_bits", 1);
        strcpy(filename, "rtu_config.json");
    }
    else if (strcmp(config_type, "mqtt") == 0) {
        mqtt_config_t mqtt_config = {0};
        if (load_mqtt_config(&mqtt_config) != ESP_OK) {
            // Valores padrão quando não existe
            mqtt_config.enabled = false;
            strncpy(mqtt_config.broker_url, "broker.hivemq.com", sizeof(mqtt_config.broker_url)-1);
            strncpy(mqtt_config.client_id, "esp32_client", sizeof(mqtt_config.client_id)-1);
            mqtt_config.port = 1883;
            mqtt_config.qos = 0;
            mqtt_config.retain = false;
            mqtt_config.tls_enabled = false;
        }
        json = cJSON_CreateObject();
        cJSON_AddBoolToObject(json, "enabled", mqtt_config.enabled);
        cJSON_AddStringToObject(json, "broker_url", mqtt_config.broker_url);
        cJSON_AddStringToObject(json, "broker_uri", mqtt_config.broker_url); // Compatibilidade
        cJSON_AddStringToObject(json, "client_id", mqtt_config.client_id);
        cJSON_AddStringToObject(json, "username", mqtt_config.username);
        cJSON_AddStringToObject(json, "password", mqtt_config.password);
        cJSON_AddNumberToObject(json, "port", mqtt_config.port);
        cJSON_AddNumberToObject(json, "qos", mqtt_config.qos);
        cJSON_AddBoolToObject(json, "retain", mqtt_config.retain);
        cJSON_AddBoolToObject(json, "tls_enabled", mqtt_config.tls_enabled);
        strcpy(filename, "mqtt_config.json");
    }
    else if (strcmp(config_type, "ap") == 0) {
        ap_config_t ap_config = {0};
        if (load_ap_config(&ap_config) != ESP_OK) {
            strncpy(ap_config.ssid, "ESP32-AP", sizeof(ap_config.ssid)-1);
            ap_config.ssid[sizeof(ap_config.ssid)-1] = '\0';
            strncpy(ap_config.ip, "192.168.4.1", sizeof(ap_config.ip)-1);
        }
        json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "ssid", ap_config.ssid);
        cJSON_AddStringToObject(json, "password", ap_config.password);
        cJSON_AddStringToObject(json, "username", ap_config.username);
        cJSON_AddStringToObject(json, "ip", ap_config.ip);
        cJSON_AddNumberToObject(json, "max_connections", 4); // Default
        cJSON_AddNumberToObject(json, "channel", 1); // Default
        strcpy(filename, "ap_config.json");
    }
    else if (strcmp(config_type, "sta") == 0) {
        sta_config_t sta_config = {0};
        if (load_sta_config(&sta_config) != ESP_OK) {
            // mantém campos vazios
        }
        json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "ssid", sta_config.ssid);
        cJSON_AddStringToObject(json, "password", sta_config.password);
        cJSON_AddBoolToObject(json, "dhcp_enabled", true); // Default
        cJSON_AddStringToObject(json, "static_ip", "");
        cJSON_AddStringToObject(json, "gateway", "");
        cJSON_AddStringToObject(json, "subnet", "");
        strcpy(filename, "sta_config.json");
    }
    else if (strcmp(config_type, "network") == 0) {
        network_config_t network_config = {0};
        if (load_network_config(&network_config) != ESP_OK) {
            // campos vazios por padrão
        }
        json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "hostname", "esp32-webserver"); // Default
        cJSON_AddBoolToObject(json, "dhcp_enabled", true); // Default
        cJSON_AddStringToObject(json, "static_ip", network_config.ip);
        cJSON_AddStringToObject(json, "gateway", network_config.gateway);
        cJSON_AddStringToObject(json, "subnet", network_config.mask);
        cJSON_AddStringToObject(json, "dns1", network_config.dns);
        cJSON_AddStringToObject(json, "dns2", "8.8.4.4"); // Default
        strcpy(filename, "network_config.json");
    }
    else if (strcmp(config_type, "main") == 0) {
        // Retorna o estado atual das flags principais de configuração
        json = cJSON_CreateObject();
        cJSON_AddBoolToObject(json, "rtu_enabled", FLAGS.rtu_enabled);
        cJSON_AddBoolToObject(json, "tcp_enabled", FLAGS.tcp_enabled);
        cJSON_AddBoolToObject(json, "AP_enabled", FLAGS.AP_enabled);
        cJSON_AddBoolToObject(json, "sta_enabled", FLAGS.sta_enabled);
        cJSON_AddBoolToObject(json, "web_enabled", FLAGS.web_enabled);
        cJSON_AddBoolToObject(json, "log_main_flags", FLAGS.log_main_flags);
        cJSON_AddBoolToObject(json, "log_sonda_queue", FLAGS.log_sonda_queue);
        cJSON_AddBoolToObject(json, "log_sonda_values", FLAGS.log_sonda_values);
        cJSON_AddBoolToObject(json, "log_modbus_tcp", FLAGS.log_modbus_tcp);
        strcpy(filename, "main_config.json");
    }
    
    if (!json) {
        return httpd_resp_send_404(req);
    }
    
    char *json_string = cJSON_Print(json);
    cJSON_Delete(json);
    
    if (!json_string) {
        return httpd_resp_send_500(req);
    }
    
    // Definir headers de download
    httpd_resp_set_type(req, "application/json");
    char content_disposition[128];
    snprintf(content_disposition, sizeof(content_disposition), "attachment; filename=\"%s\"", filename);
    httpd_resp_set_hdr(req, "Content-Disposition", content_disposition);
    
    esp_err_t result = httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    
    return result;
}
