#include "spiffs_file_manager.h"
#include <esp_spiffs.h>
#include <esp_log.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/unistd.h>

static const char *TAG = "SPIFFS_MGR";

// Estado interno da biblioteca
static bool g_initialized = false;
static spiffs_manager_config_t g_config = {0};

// Estatísticas de uso
static struct {
    uint32_t files_served;
    uint32_t cache_hits;
    uint32_t cache_misses;
    size_t total_bytes_served;
} g_stats = {0};

// MIME types padrão
static const struct {
    const char *extension;
    const char *mime_type;
} g_default_mime_types[] = {
    {".html", "text/html"},
    {".htm", "text/html"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".json", "application/json"},
    {".xml", "application/xml"},
    {".png", "image/png"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".gif", "image/gif"},
    {".ico", "image/x-icon"},
    {".svg", "image/svg+xml"},
    {".pdf", "application/pdf"},
    {".txt", "text/plain"},
    {".csv", "text/csv"},
    {NULL, NULL}
};

// MIME types customizados (máximo 10)
static struct {
    char extension[16];
    char mime_type[64];
} g_custom_mime_types[10];
static size_t g_custom_mime_count = 0;

// Cache de arquivos (se habilitado)
#define MAX_CACHE_ENTRIES 20
static struct {
    char filepath[SPIFFS_MANAGER_MAX_PATH_LEN];
    char *content;
    size_t content_length;
    uint32_t last_access;
} g_file_cache[MAX_CACHE_ENTRIES];
static size_t g_cache_count = 0;

// Preprocessors registrados
#define MAX_PREPROCESSORS 5
static struct {
    char extension[16];
    spiffs_file_preprocessor_t preprocessor;
} g_preprocessors[MAX_PREPROCESSORS];
static size_t g_preprocessor_count = 0;

// ============================================================================
// FUNÇÕES INTERNAS
// ============================================================================

static void ensure_spiffs_mounted(void) {
    static bool mounted = false;
    if (mounted) return;
    
    ESP_LOGI(TAG, "Montando SPIFFS...");
    
    esp_vfs_spiffs_conf_t conf = {
        .base_path = g_config.base_path,
        .partition_label = NULL,
        .max_files = g_config.max_open_files,
        .format_if_mount_failed = true,
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS: %s", esp_err_to_name(ret));
        return;
    }
    
    // Verificar espaço disponível
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS: %d KB total, %d KB used", total / 1024, used / 1024);
    }
    
    mounted = true;
}

static char* build_full_path(const char *filepath) {
    if (!filepath) return NULL;
    
    // Se já é um caminho absoluto, usar como está
    if (filepath[0] == '/') {
        return strdup(filepath);
    }
    
    // Construir caminho completo
    size_t path_len = strlen(g_config.base_path) + strlen(filepath) + 2;
    char *full_path = malloc(path_len);
    if (!full_path) {
        ESP_LOGE(TAG, "Out of memory building path");
        return NULL;
    }
    
    snprintf(full_path, path_len, "%s/%s", g_config.base_path, filepath);
    return full_path;
}

static const char* find_mime_type(const char *filepath) {
    const char *ext = strrchr(filepath, '.');
    if (!ext) return "text/plain";
    
    // Verificar MIME types customizados primeiro
    for (size_t i = 0; i < g_custom_mime_count; i++) {
        if (strcasecmp(ext, g_custom_mime_types[i].extension) == 0) {
            return g_custom_mime_types[i].mime_type;
        }
    }
    
    // Verificar MIME types padrão
    for (size_t i = 0; g_default_mime_types[i].extension; i++) {
        if (strcasecmp(ext, g_default_mime_types[i].extension) == 0) {
            return g_default_mime_types[i].mime_type;
        }
    }
    
    return "text/plain";
}

static char* find_in_cache(const char *filepath) {
    if (!g_config.enable_cache) return NULL;
    
    for (size_t i = 0; i < g_cache_count; i++) {
        if (strcmp(g_file_cache[i].filepath, filepath) == 0) {
            g_file_cache[i].last_access = esp_log_timestamp();
            g_stats.cache_hits++;
            ESP_LOGD(TAG, "Cache hit: %s", filepath);
            return strdup(g_file_cache[i].content);
        }
    }
    
    g_stats.cache_misses++;
    return NULL;
}

static void add_to_cache(const char *filepath, const char *content, size_t content_length) {
    if (!g_config.enable_cache || content_length > g_config.max_file_size / 4) {
        return; // Não cachear arquivos muito grandes
    }
    
    // Procurar slot vazio ou mais antigo
    size_t oldest_index = 0;
    uint32_t oldest_access = UINT32_MAX;
    bool found_empty = false;
    
    for (size_t i = 0; i < MAX_CACHE_ENTRIES; i++) {
        if (g_file_cache[i].content == NULL) {
            oldest_index = i;
            found_empty = true;
            break;
        }
        if (g_file_cache[i].last_access < oldest_access) {
            oldest_access = g_file_cache[i].last_access;
            oldest_index = i;
        }
    }
    
    // Liberar slot se necessário
    if (g_file_cache[oldest_index].content) {
        free(g_file_cache[oldest_index].content);
        g_file_cache[oldest_index].content = NULL;
    }
    
    // Adicionar ao cache
    strncpy(g_file_cache[oldest_index].filepath, filepath, SPIFFS_MANAGER_MAX_PATH_LEN - 1);
    g_file_cache[oldest_index].filepath[SPIFFS_MANAGER_MAX_PATH_LEN - 1] = '\0';
    g_file_cache[oldest_index].content = strdup(content);
    g_file_cache[oldest_index].content_length = content_length;
    g_file_cache[oldest_index].last_access = esp_log_timestamp();
    
    if (found_empty && g_cache_count < MAX_CACHE_ENTRIES) {
        g_cache_count++;
    }
    
    ESP_LOGD(TAG, "Cached file: %s (%d bytes)", filepath, content_length);
}

static char* replace_template_placeholder(const char *template, const char *placeholder, const char *value) {
    if (!template || !placeholder || !value) return NULL;
    
    char search_pattern[128];
    snprintf(search_pattern, sizeof(search_pattern), "{{%s}}", placeholder);
    
    // Contar ocorrências
    int count = 0;
    const char *pos = template;
    while ((pos = strstr(pos, search_pattern)) != NULL) {
        count++;
        pos += strlen(search_pattern);
    }
    
    if (count == 0) {
        return strdup(template);
    }
    
    // Calcular tamanho necessário
    size_t template_len = strlen(template);
    size_t pattern_len = strlen(search_pattern);
    size_t value_len = strlen(value);
    size_t new_size = template_len + (count * (value_len - pattern_len)) + 1;
    
    char *result = malloc(new_size);
    if (!result) {
        ESP_LOGE(TAG, "Out of memory processing template");
        return NULL;
    }
    
    // Realizar substituições
    char *result_pos = result;
    const char *template_pos = template;
    
    while ((pos = strstr(template_pos, search_pattern)) != NULL) {
        // Copiar texto antes do placeholder
        size_t copy_len = pos - template_pos;
        memcpy(result_pos, template_pos, copy_len);
        result_pos += copy_len;
        
        // Inserir valor
        memcpy(result_pos, value, value_len);
        result_pos += value_len;
        
        // Avançar após o placeholder
        template_pos = pos + pattern_len;
    }
    
    // Copiar texto restante
    strcpy(result_pos, template_pos);
    
    ESP_LOGD(TAG, "Replaced %d occurrences of %s", count, search_pattern);
    return result;
}

// ============================================================================
// API PÚBLICA
// ============================================================================

void spiffs_manager_get_default_config(spiffs_manager_config_t *config) {
    if (!config) return;
    
    config->base_path = SPIFFS_MANAGER_DEFAULT_BASE_PATH;
    config->default_index = "index.html";
    config->enable_cache = true;
    config->enable_compression = false;
    config->enable_development_headers = true;
    config->max_file_size = SPIFFS_MANAGER_MAX_FILE_SIZE;
    config->max_open_files = 10;
}

esp_err_t spiffs_manager_init(const spiffs_manager_config_t *config) {
    if (g_initialized) {
        ESP_LOGW(TAG, "SPIFFS Manager already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing SPIFFS File Manager");
    
    // Usar configuração padrão se não fornecida
    if (config) {
        g_config = *config;
    } else {
        spiffs_manager_get_default_config(&g_config);
    }
    
    // Garantir que SPIFFS está montado
    ensure_spiffs_mounted();
    
    // Limpar cache
    memset(g_file_cache, 0, sizeof(g_file_cache));
    g_cache_count = 0;
    
    // Limpar estatísticas
    memset(&g_stats, 0, sizeof(g_stats));
    
    // Limpar preprocessors
    g_preprocessor_count = 0;
    
    g_initialized = true;
    ESP_LOGI(TAG, "SPIFFS File Manager initialized successfully");
    ESP_LOGI(TAG, "Base path: %s", g_config.base_path);
    ESP_LOGI(TAG, "Default index: %s", g_config.default_index);
    ESP_LOGI(TAG, "Cache enabled: %s", g_config.enable_cache ? "Yes" : "No");
    
    return ESP_OK;
}

esp_err_t spiffs_manager_deinit(void) {
    if (!g_initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Deinitializing SPIFFS File Manager");
    
    // Limpar cache
    for (size_t i = 0; i < MAX_CACHE_ENTRIES; i++) {
        if (g_file_cache[i].content) {
            free(g_file_cache[i].content);
            g_file_cache[i].content = NULL;
        }
    }
    
    // Desmonta SPIFFS
    esp_vfs_spiffs_unregister(NULL);
    
    g_initialized = false;
    ESP_LOGI(TAG, "SPIFFS File Manager deinitialized");
    return ESP_OK;
}

bool spiffs_manager_is_initialized(void) {
    return g_initialized;
}

spiffs_manager_result_t spiffs_manager_load_file(const char *filepath, char **content, size_t *content_length) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "Manager not initialized");
        return SPIFFS_MANAGER_ERROR_SPIFFS_NOT_MOUNTED;
    }
    
    if (!filepath || !content) {
        return SPIFFS_MANAGER_ERROR_INVALID_PATH;
    }
    
    *content = NULL;
    if (content_length) *content_length = 0;
    
    // Verificar cache primeiro
    char *cached_content = find_in_cache(filepath);
    if (cached_content) {
        *content = cached_content;
        if (content_length) *content_length = strlen(cached_content);
        return SPIFFS_MANAGER_OK;
    }
    
    // Construir caminho completo
    char *full_path = build_full_path(filepath);
    if (!full_path) {
        return SPIFFS_MANAGER_ERROR_OUT_OF_MEMORY;
    }
    
    ESP_LOGD(TAG, "Loading file: %s", full_path);
    
    // Abrir arquivo
    FILE *file = fopen(full_path, "r");
    if (!file) {
        ESP_LOGW(TAG, "File not found: %s", full_path);
        free(full_path);
        return SPIFFS_MANAGER_ERROR_FILE_NOT_FOUND;
    }
    
    // Obter tamanho
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);
    
    if (file_size <= 0) {
        fclose(file);
        free(full_path);
        ESP_LOGE(TAG, "Invalid file size: %s", filepath);
        return SPIFFS_MANAGER_ERROR_INVALID_PATH;
    }
    
    if (file_size > g_config.max_file_size) {
        fclose(file);
        free(full_path);
        ESP_LOGE(TAG, "File too large: %s (%ld bytes)", filepath, file_size);
        return SPIFFS_MANAGER_ERROR_FILE_TOO_LARGE;
    }
    
    // Alocar memória
    *content = malloc(file_size + 1);
    if (!*content) {
        fclose(file);
        free(full_path);
        ESP_LOGE(TAG, "Out of memory loading file: %s", filepath);
        return SPIFFS_MANAGER_ERROR_OUT_OF_MEMORY;
    }
    
    // Ler arquivo
    size_t read_size = fread(*content, 1, file_size, file);
    fclose(file);
    
    if (read_size != file_size) {
        free(*content);
        *content = NULL;
        free(full_path);
        ESP_LOGE(TAG, "Failed to read complete file: %s", filepath);
        return SPIFFS_MANAGER_ERROR_INVALID_PATH;
    }
    
    (*content)[file_size] = '\0';
    
    if (content_length) {
        *content_length = file_size;
    }
    
    // Verificar se há preprocessor registrado
    const char *ext = strrchr(filepath, '.');
    if (ext) {
        for (size_t i = 0; i < g_preprocessor_count; i++) {
            if (strcasecmp(ext, g_preprocessors[i].extension) == 0) {
                ESP_LOGD(TAG, "Running preprocessor for %s", ext);
                esp_err_t ret = g_preprocessors[i].preprocessor(filepath, content, &file_size);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "Preprocessor failed for %s: %s", filepath, esp_err_to_name(ret));
                }
                if (content_length) *content_length = file_size;
                break;
            }
        }
    }
    
    // Adicionar ao cache
    add_to_cache(filepath, *content, file_size);
    
    // Atualizar estatísticas
    g_stats.files_served++;
    g_stats.total_bytes_served += file_size;
    
    ESP_LOGD(TAG, "File loaded successfully: %s (%d bytes)", filepath, file_size);
    
    free(full_path);
    return SPIFFS_MANAGER_OK;
}

bool spiffs_manager_file_exists(const char *filepath) {
    if (!g_initialized || !filepath) {
        return false;
    }
    
    char *full_path = build_full_path(filepath);
    if (!full_path) {
        return false;
    }
    
    struct stat st;
    bool exists = (stat(full_path, &st) == 0);
    
    free(full_path);
    return exists;
}

esp_err_t spiffs_manager_get_file_size(const char *filepath, size_t *size) {
    if (!g_initialized || !filepath || !size) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char *full_path = build_full_path(filepath);
    if (!full_path) {
        return ESP_ERR_NO_MEM;
    }
    
    struct stat st;
    if (stat(full_path, &st) != 0) {
        free(full_path);
        return ESP_ERR_NOT_FOUND;
    }
    
    *size = st.st_size;
    free(full_path);
    return ESP_OK;
}

esp_err_t spiffs_manager_remount_spiffs(void) {
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Remounting SPIFFS");
    
    // Limpar cache
    spiffs_manager_clear_cache();
    
    // Desmontar e remontar
    esp_vfs_spiffs_unregister(NULL);
    ensure_spiffs_mounted();
    
    return ESP_OK;
}

const char* spiffs_manager_get_mime_type(const char *filepath) {
    if (!filepath) {
        return "text/plain";
    }
    
    return find_mime_type(filepath);
}

esp_err_t spiffs_manager_register_mime_type(const char *extension, const char *mime_type) {
    if (!extension || !mime_type || g_custom_mime_count >= 10) {
        return ESP_ERR_INVALID_ARG;
    }
    
    strncpy(g_custom_mime_types[g_custom_mime_count].extension, extension, 15);
    g_custom_mime_types[g_custom_mime_count].extension[15] = '\0';
    
    strncpy(g_custom_mime_types[g_custom_mime_count].mime_type, mime_type, 63);
    g_custom_mime_types[g_custom_mime_count].mime_type[63] = '\0';
    
    g_custom_mime_count++;
    
    ESP_LOGI(TAG, "Registered MIME type: %s -> %s", extension, mime_type);
    return ESP_OK;
}

esp_err_t spiffs_manager_set_http_headers(httpd_req_t *req, const char *filepath, bool enable_cache) {
    if (!req || !filepath) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Definir Content-Type
    const char *mime_type = spiffs_manager_get_mime_type(filepath);
    httpd_resp_set_type(req, mime_type);
    
    // Headers de cache
    if (!enable_cache || g_config.enable_development_headers) {
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
        httpd_resp_set_hdr(req, "Pragma", "no-cache");
        httpd_resp_set_hdr(req, "Expires", "0");
    } else {
        // Cache por 1 hora para produção
        httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=3600");
    }
    
    // Headers de segurança
    if (strstr(filepath, ".html")) {
        httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
        httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    }
    
    return ESP_OK;
}

spiffs_manager_result_t spiffs_manager_process_template(const char *template_content,
                                                       const spiffs_template_var_t *variables,
                                                       size_t var_count,
                                                       char **processed_content) {
    if (!template_content || !processed_content) {
        return SPIFFS_MANAGER_ERROR_INVALID_TEMPLATE;
    }
    
    *processed_content = strdup(template_content);
    if (!*processed_content) {
        return SPIFFS_MANAGER_ERROR_OUT_OF_MEMORY;
    }
    
    if (!variables || var_count == 0) {
        return SPIFFS_MANAGER_OK; // Sem substituições
    }
    
    // Processar cada variável
    for (size_t i = 0; i < var_count; i++) {
        if (variables[i].placeholder && variables[i].value) {
            char *new_content = replace_template_placeholder(*processed_content,
                                                           variables[i].placeholder,
                                                           variables[i].value);
            if (new_content) {
                free(*processed_content);
                *processed_content = new_content;
            }
        }
    }
    
    return SPIFFS_MANAGER_OK;
}

spiffs_manager_result_t spiffs_manager_load_and_process_template(const char *template_path,
                                                                const spiffs_template_var_t *variables,
                                                                size_t var_count,
                                                                char **processed_content) {
    if (!template_path || !processed_content) {
        return SPIFFS_MANAGER_ERROR_INVALID_PATH;
    }
    
    // Carregar template
    char *template_content = NULL;
    spiffs_manager_result_t result = spiffs_manager_load_file(template_path, &template_content, NULL);
    if (result != SPIFFS_MANAGER_OK) {
        return result;
    }
    
    // Processar template
    result = spiffs_manager_process_template(template_content, variables, var_count, processed_content);
    
    free(template_content);
    return result;
}

// ============================================================================
// HANDLERS HTTP
// ============================================================================

esp_err_t spiffs_manager_static_handler(httpd_req_t *req) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "Manager not initialized");
        return spiffs_manager_send_500(req, "SPIFFS Manager not initialized");
    }
    
    const char *uri = req->uri;
    ESP_LOGD(TAG, "Static handler called for: %s", uri);
    
    // Construir caminho do arquivo
    char filepath[SPIFFS_MANAGER_MAX_PATH_LEN];
    if (strcmp(uri, "/") == 0) {
        snprintf(filepath, sizeof(filepath), "html/%s", g_config.default_index);
    } else {
        // Remover '/' inicial para usar como caminho relativo
        const char *relative_path = uri[0] == '/' ? uri + 1 : uri;
        strncpy(filepath, relative_path, sizeof(filepath) - 1);
        filepath[sizeof(filepath) - 1] = '\0';
    }
    
    // Carregar arquivo
    char *content = NULL;
    size_t content_length = 0;
    spiffs_manager_result_t result = spiffs_manager_load_file(filepath, &content, &content_length);
    
    if (result != SPIFFS_MANAGER_OK) {
        if (result == SPIFFS_MANAGER_ERROR_FILE_NOT_FOUND) {
            return spiffs_manager_send_404(req, NULL);
        } else {
            return spiffs_manager_send_500(req, spiffs_manager_get_error_string(result));
        }
    }
    
    // Definir headers
    spiffs_manager_set_http_headers(req, filepath, !g_config.enable_development_headers);
    
    // Enviar resposta
    esp_err_t ret = httpd_resp_send(req, content, content_length);
    
    free(content);
    
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "File served successfully: %s (%d bytes)", filepath, content_length);
    }
    
    return ret;
}

esp_err_t spiffs_manager_css_handler(httpd_req_t *req) {
    ESP_LOGD(TAG, "CSS handler for: %s", req->uri);
    return spiffs_manager_static_handler(req);
}

esp_err_t spiffs_manager_js_handler(httpd_req_t *req) {
    ESP_LOGD(TAG, "JS handler for: %s", req->uri);
    return spiffs_manager_static_handler(req);
}

esp_err_t spiffs_manager_html_handler(httpd_req_t *req) {
    ESP_LOGD(TAG, "HTML handler for: %s", req->uri);
    return spiffs_manager_static_handler(req);
}

esp_err_t spiffs_manager_index_handler(httpd_req_t *req) {
    ESP_LOGD(TAG, "Index handler called");
    return spiffs_manager_static_handler(req);
}

esp_err_t spiffs_manager_template_handler(httpd_req_t *req,
                                         const char *template_path,
                                         const spiffs_template_var_t *variables,
                                         size_t var_count) {
    if (!g_initialized || !req || !template_path) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Processar template
    char *processed_content = NULL;
    spiffs_manager_result_t result = spiffs_manager_load_and_process_template(template_path,
                                                                             variables,
                                                                             var_count,
                                                                             &processed_content);
    
    if (result != SPIFFS_MANAGER_OK) {
        if (result == SPIFFS_MANAGER_ERROR_FILE_NOT_FOUND) {
            return spiffs_manager_send_404(req, NULL);
        } else {
            return spiffs_manager_send_500(req, spiffs_manager_get_error_string(result));
        }
    }
    
    // Definir headers
    spiffs_manager_set_http_headers(req, template_path, !g_config.enable_development_headers);
    
    // Enviar resposta
    esp_err_t ret = httpd_resp_send(req, processed_content, strlen(processed_content));
    
    free(processed_content);
    return ret;
}

esp_err_t spiffs_manager_register_default_handlers(httpd_handle_t server) {
    if (!g_initialized || !server) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Registering default handlers");
    
    esp_err_t ret;
    
    // Handler para página inicial
    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = spiffs_manager_index_handler
    };
    ret = httpd_register_uri_handler(server, &index_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register index handler: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Handler para CSS
    httpd_uri_t css_uri = {
        .uri = "/css/*",
        .method = HTTP_GET,
        .handler = spiffs_manager_css_handler
    };
    ret = httpd_register_uri_handler(server, &css_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register CSS handler: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Handler para JavaScript
    httpd_uri_t js_uri = {
        .uri = "/js/*",
        .method = HTTP_GET,
        .handler = spiffs_manager_js_handler
    };
    ret = httpd_register_uri_handler(server, &js_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register JS handler: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Handler para HTML
    httpd_uri_t html_uri = {
        .uri = "/html/*",
        .method = HTTP_GET,
        .handler = spiffs_manager_html_handler
    };
    ret = httpd_register_uri_handler(server, &html_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register HTML handler: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Handler para assets
    httpd_uri_t assets_uri = {
        .uri = "/assets/*",
        .method = HTTP_GET,
        .handler = spiffs_manager_static_handler
    };
    ret = httpd_register_uri_handler(server, &assets_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register assets handler: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Default handlers registered successfully");
    return ESP_OK;
}

esp_err_t spiffs_manager_register_custom_handler(httpd_handle_t server,
                                                 const char *uri_pattern,
                                                 httpd_method_t method,
                                                 httpd_handler_t handler) {
    if (!g_initialized || !server || !uri_pattern || !handler) {
        return ESP_ERR_INVALID_ARG;
    }
    
    httpd_uri_t custom_uri = {
        .uri = uri_pattern,
        .method = method,
        .handler = handler
    };
    
    esp_err_t ret = httpd_register_uri_handler(server, &custom_uri);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Custom handler registered: %s", uri_pattern);
    } else {
        ESP_LOGE(TAG, "Failed to register custom handler %s: %s", uri_pattern, esp_err_to_name(ret));
    }
    
    return ret;
}

// ============================================================================
// UTILITÁRIOS
// ============================================================================

esp_err_t spiffs_manager_send_404(httpd_req_t *req, const char *custom_message) {
    const char *default_404 = 
        "<!DOCTYPE html>"
        "<html><head><title>404 - Not Found</title></head>"
        "<body><h1>404 - File Not Found</h1>"
        "<p>The requested file was not found on this server.</p>"
        "<p><a href=\"/\">Return to home</a></p></body></html>";
    
    const char *message = custom_message ? custom_message : default_404;
    
    httpd_resp_set_status(req, "404 Not Found");
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, message, strlen(message));
}

esp_err_t spiffs_manager_send_500(httpd_req_t *req, const char *error_details) {
    char error_page[1024];
    snprintf(error_page, sizeof(error_page),
        "<!DOCTYPE html>"
        "<html><head><title>500 - Internal Server Error</title></head>"
        "<body><h1>500 - Internal Server Error</h1>"
        "<p>An error occurred while processing your request.</p>"
        "%s%s%s"
        "<p><a href=\"/\">Return to home</a></p></body></html>",
        error_details ? "<p><strong>Details:</strong> " : "",
        error_details ? error_details : "",
        error_details ? "</p>" : ""
    );
    
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, error_page, strlen(error_page));
}

esp_err_t spiffs_manager_result_to_esp_err(spiffs_manager_result_t result) {
    switch (result) {
        case SPIFFS_MANAGER_OK:
            return ESP_OK;
        case SPIFFS_MANAGER_ERROR_FILE_NOT_FOUND:
            return ESP_ERR_NOT_FOUND;
        case SPIFFS_MANAGER_ERROR_OUT_OF_MEMORY:
            return ESP_ERR_NO_MEM;
        case SPIFFS_MANAGER_ERROR_FILE_TOO_LARGE:
            return ESP_ERR_INVALID_SIZE;
        case SPIFFS_MANAGER_ERROR_INVALID_PATH:
            return ESP_ERR_INVALID_ARG;
        case SPIFFS_MANAGER_ERROR_SPIFFS_NOT_MOUNTED:
            return ESP_ERR_INVALID_STATE;
        case SPIFFS_MANAGER_ERROR_INVALID_TEMPLATE:
            return ESP_ERR_INVALID_ARG;
        default:
            return ESP_FAIL;
    }
}

const char* spiffs_manager_get_error_string(spiffs_manager_result_t result) {
    switch (result) {
        case SPIFFS_MANAGER_OK:
            return "Success";
        case SPIFFS_MANAGER_ERROR_FILE_NOT_FOUND:
            return "File not found";
        case SPIFFS_MANAGER_ERROR_OUT_OF_MEMORY:
            return "Out of memory";
        case SPIFFS_MANAGER_ERROR_FILE_TOO_LARGE:
            return "File too large";
        case SPIFFS_MANAGER_ERROR_INVALID_PATH:
            return "Invalid file path";
        case SPIFFS_MANAGER_ERROR_SPIFFS_NOT_MOUNTED:
            return "SPIFFS not mounted";
        case SPIFFS_MANAGER_ERROR_INVALID_TEMPLATE:
            return "Invalid template";
        default:
            return "Unknown error";
    }
}

esp_err_t spiffs_manager_register_preprocessor(const char *extension, spiffs_file_preprocessor_t preprocessor) {
    if (!extension || !preprocessor || g_preprocessor_count >= MAX_PREPROCESSORS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    strncpy(g_preprocessors[g_preprocessor_count].extension, extension, 15);
    g_preprocessors[g_preprocessor_count].extension[15] = '\0';
    g_preprocessors[g_preprocessor_count].preprocessor = preprocessor;
    
    g_preprocessor_count++;
    
    ESP_LOGI(TAG, "Registered preprocessor for: %s", extension);
    return ESP_OK;
}

esp_err_t spiffs_manager_clear_cache(void) {
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    for (size_t i = 0; i < MAX_CACHE_ENTRIES; i++) {
        if (g_file_cache[i].content) {
            free(g_file_cache[i].content);
            g_file_cache[i].content = NULL;
        }
        memset(&g_file_cache[i], 0, sizeof(g_file_cache[i]));
    }
    
    g_cache_count = 0;
    
    ESP_LOGI(TAG, "File cache cleared");
    return ESP_OK;
}

esp_err_t spiffs_manager_get_stats(uint32_t *files_served,
                                  uint32_t *cache_hits,
                                  uint32_t *cache_misses,
                                  size_t *total_bytes_served) {
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (files_served) *files_served = g_stats.files_served;
    if (cache_hits) *cache_hits = g_stats.cache_hits;
    if (cache_misses) *cache_misses = g_stats.cache_misses;
    if (total_bytes_served) *total_bytes_served = g_stats.total_bytes_served;
    
    return ESP_OK;
}