/**
 * @file wsm_static_files.c
 * @brief Sistema de arquivos estáticos do Web Server Manager
 */

#include "wsm_static_files.h"
#include "web_server_manager.h"
#include <esp_log.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>

static const char *TAG = "WSM_STATIC";

// =============================================================================
// ESTRUTURAS INTERNAS
// =============================================================================

typedef struct {
    wsm_static_handler_config_t handlers[WSM_MAX_STATIC_HANDLERS];
    size_t handler_count;
    wsm_static_cache_entry_t cache[WSM_STATIC_CACHE_SIZE];
    wsm_mime_entry_t mime_types[WSM_MAX_MIME_TYPES];
    size_t mime_count;
    size_t cache_hits;
    size_t cache_misses;
    bool caching_enabled;
    bool initialized;
} wsm_static_context_t;

// =============================================================================
// VARIÁVEIS GLOBAIS INTERNAS
// =============================================================================

static wsm_static_context_t g_static_ctx = {0};

// Declarações externas
extern wsm_context_t g_wsm_ctx;

// =============================================================================
// TIPOS MIME PADRÃO
// =============================================================================

static const wsm_mime_entry_t default_mime_types[] = {
    {".html", "text/html"},
    {".htm", "text/html"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".json", "application/json"},
    {".xml", "application/xml"},
    {".txt", "text/plain"},
    {".png", "image/png"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".gif", "image/gif"},
    {".bmp", "image/bmp"},
    {".ico", "image/x-icon"},
    {".svg", "image/svg+xml"},
    {".pdf", "application/pdf"},
    {".zip", "application/zip"},
    {".tar", "application/x-tar"},
    {".gz", "application/gzip"},
    {".woff", "font/woff"},
    {".woff2", "font/woff2"},
    {".ttf", "font/ttf"},
    {".eot", "application/vnd.ms-fontobject"}
};

// =============================================================================
// FUNÇÕES DE INICIALIZAÇÃO
// =============================================================================

esp_err_t wsm_static_init(void)
{
    if (g_static_ctx.initialized) {
        ESP_LOGW(TAG, "Static files system already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing static files system...");

    memset(&g_static_ctx, 0, sizeof(wsm_static_context_t));
    g_static_ctx.caching_enabled = true;
    g_static_ctx.initialized = true;

    ESP_LOGI(TAG, "Static files system initialized successfully");
    return ESP_OK;
}

esp_err_t wsm_static_deinit(void)
{
    if (!g_static_ctx.initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing static files system...");

    // Limpar cache
    wsm_static_clear_cache();
    
    memset(&g_static_ctx, 0, sizeof(wsm_static_context_t));

    ESP_LOGI(TAG, "Static files system deinitialized");
    return ESP_OK;
}

// =============================================================================
// FUNÇÕES DE MIME TYPES
// =============================================================================

esp_err_t wsm_mime_types_init(void)
{
    size_t default_count = sizeof(default_mime_types) / sizeof(default_mime_types[0]);
    
    if (default_count > WSM_MAX_MIME_TYPES) {
        ESP_LOGE(TAG, "Too many default MIME types");
        return ESP_ERR_NO_MEM;
    }

    // Copiar tipos padrão
    memcpy(g_static_ctx.mime_types, default_mime_types, 
           default_count * sizeof(wsm_mime_entry_t));
    g_static_ctx.mime_count = default_count;

    ESP_LOGI(TAG, "Initialized %zu MIME types", g_static_ctx.mime_count);
    return ESP_OK;
}

esp_err_t wsm_mime_add_type(const char *extension, const char *mime_type)
{
    if (!extension || !mime_type) {
        return ESP_ERR_INVALID_ARG;
    }

    if (g_static_ctx.mime_count >= WSM_MAX_MIME_TYPES) {
        ESP_LOGE(TAG, "Maximum MIME types reached");
        return ESP_ERR_NO_MEM;
    }

    wsm_mime_entry_t *entry = &g_static_ctx.mime_types[g_static_ctx.mime_count];
    
    // Garantir que extensão comece com ponto
    if (extension[0] == '.') {
        strncpy(entry->extension, extension, sizeof(entry->extension) - 1);
    } else {
        snprintf(entry->extension, sizeof(entry->extension), ".%s", extension);
    }
    entry->extension[sizeof(entry->extension) - 1] = '\0';

    strncpy(entry->mime_type, mime_type, sizeof(entry->mime_type) - 1);
    entry->mime_type[sizeof(entry->mime_type) - 1] = '\0';

    g_static_ctx.mime_count++;
    
    ESP_LOGI(TAG, "Added MIME type: %s -> %s", entry->extension, entry->mime_type);
    return ESP_OK;
}

const char *wsm_mime_get_type(const char *file_path)
{
    if (!file_path) {
        return "text/plain";
    }

    // Encontrar extensão
    const char *ext = strrchr(file_path, '.');
    if (!ext) {
        return "text/plain";
    }

    // Procurar tipo MIME
    for (size_t i = 0; i < g_static_ctx.mime_count; i++) {
        if (strcasecmp(g_static_ctx.mime_types[i].extension, ext) == 0) {
            return g_static_ctx.mime_types[i].mime_type;
        }
    }

    return "text/plain";
}

// =============================================================================
// FUNÇÕES DE CACHE
// =============================================================================

void wsm_static_set_caching_enabled(bool enabled)
{
    g_static_ctx.caching_enabled = enabled;
    if (!enabled) {
        wsm_static_clear_cache();
    }
    ESP_LOGI(TAG, "Static file caching %s", enabled ? "enabled" : "disabled");
}

esp_err_t wsm_static_clear_cache(void)
{
    for (int i = 0; i < WSM_STATIC_CACHE_SIZE; i++) {
        wsm_static_cache_entry_t *entry = &g_static_ctx.cache[i];
        if (entry->valid && entry->content) {
            free(entry->content);
            entry->content = NULL;
        }
        entry->valid = false;
    }

    g_static_ctx.cache_hits = 0;
    g_static_ctx.cache_misses = 0;

    ESP_LOGI(TAG, "Static file cache cleared");
    return ESP_OK;
}

esp_err_t wsm_static_get_cache_stats(size_t *hits, size_t *misses, size_t *entries)
{
    if (!hits || !misses || !entries) {
        return ESP_ERR_INVALID_ARG;
    }

    *hits = g_static_ctx.cache_hits;
    *misses = g_static_ctx.cache_misses;
    
    *entries = 0;
    for (int i = 0; i < WSM_STATIC_CACHE_SIZE; i++) {
        if (g_static_ctx.cache[i].valid) {
            (*entries)++;
        }
    }

    return ESP_OK;
}

static wsm_static_cache_entry_t *wsm_static_cache_find(const char *file_path)
{
    for (int i = 0; i < WSM_STATIC_CACHE_SIZE; i++) {
        wsm_static_cache_entry_t *entry = &g_static_ctx.cache[i];
        if (entry->valid && strcmp(entry->file_path, file_path) == 0) {
            return entry;
        }
    }
    return NULL;
}

static wsm_static_cache_entry_t *wsm_static_cache_get_free_slot(void)
{
    // Procurar slot vazio
    for (int i = 0; i < WSM_STATIC_CACHE_SIZE; i++) {
        wsm_static_cache_entry_t *entry = &g_static_ctx.cache[i];
        if (!entry->valid) {
            return entry;
        }
    }

    // Se não houver slots vazios, usar o mais antigo (LRU simples)
    wsm_static_cache_entry_t *oldest = &g_static_ctx.cache[0];
    for (int i = 1; i < WSM_STATIC_CACHE_SIZE; i++) {
        wsm_static_cache_entry_t *entry = &g_static_ctx.cache[i];
        if (entry->last_modified < oldest->last_modified) {
            oldest = entry;
        }
    }

    // Liberar slot
    if (oldest->content) {
        free(oldest->content);
        oldest->content = NULL;
    }
    oldest->valid = false;

    return oldest;
}

// =============================================================================
// FUNÇÕES UTILITÁRIAS
// =============================================================================

bool wsm_static_file_exists(const char *file_path)
{
    if (!file_path) return false;
    
    struct stat st;
    return stat(file_path, &st) == 0 && S_ISREG(st.st_mode);
}

long wsm_static_get_file_size(const char *file_path)
{
    if (!file_path) return -1;
    
    struct stat st;
    if (stat(file_path, &st) == 0 && S_ISREG(st.st_mode)) {
        return st.st_size;
    }
    return -1;
}

uint32_t wsm_static_get_file_mtime(const char *file_path)
{
    if (!file_path) return 0;
    
    struct stat st;
    if (stat(file_path, &st) == 0) {
        return (uint32_t)st.st_mtime;
    }
    return 0;
}

esp_err_t wsm_static_generate_etag(const char *file_path, char *etag, size_t etag_size)
{
    if (!file_path || !etag || etag_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t mtime = wsm_static_get_file_mtime(file_path);
    long size = wsm_static_get_file_size(file_path);
    
    if (mtime == 0 || size < 0) {
        return ESP_FAIL;
    }

    // ETag simples baseado em timestamp e tamanho
    snprintf(etag, etag_size, "\"%lx-%ld\"", mtime, size);
    return ESP_OK;
}

bool wsm_static_check_if_none_match(httpd_req_t *req, const char *etag)
{
    if (!req || !etag) return false;

    size_t buf_len = httpd_req_get_hdr_value_len(req, "If-None-Match") + 1;
    if (buf_len <= 1) return false;

    char *if_none_match = malloc(buf_len);
    if (!if_none_match) return false;

    bool match = false;
    if (httpd_req_get_hdr_value_str(req, "If-None-Match", if_none_match, buf_len) == ESP_OK) {
        match = (strcmp(if_none_match, etag) == 0);
    }

    free(if_none_match);
    return match;
}

// =============================================================================
// FUNÇÕES DE CARREGAMENTO DE ARQUIVOS
// =============================================================================

static esp_err_t wsm_static_load_file(const char *file_path, char **content, size_t *size)
{
    if (!file_path || !content || !size) {
        return ESP_ERR_INVALID_ARG;
    }

    // Verificar cache se habilitado
    if (g_static_ctx.caching_enabled) {
        wsm_static_cache_entry_t *cached = wsm_static_cache_find(file_path);
        if (cached) {
            uint32_t file_mtime = wsm_static_get_file_mtime(file_path);
            if (file_mtime <= cached->last_modified) {
                *content = malloc(cached->size);
                if (*content) {
                    memcpy(*content, cached->content, cached->size);
                    *size = cached->size;
                    g_static_ctx.cache_hits++;
                    ESP_LOGD(TAG, "Static file cache hit: %s", file_path);
                    return ESP_OK;
                }
            } else {
                // Arquivo modificado, invalidar cache
                cached->valid = false;
                if (cached->content) {
                    free(cached->content);
                    cached->content = NULL;
                }
            }
        }
    }

    // Cache miss - carregar do arquivo
    g_static_ctx.cache_misses++;

    FILE *file = fopen(file_path, "rb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", file_path);
        return ESP_FAIL;
    }

    // Obter tamanho
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);

    if (file_size <= 0) {
        fclose(file);
        ESP_LOGE(TAG, "File is empty or error getting size: %s", file_path);
        return ESP_FAIL;
    }

    // Alocar memória
    *content = malloc(file_size);
    if (!*content) {
        fclose(file);
        ESP_LOGE(TAG, "Out of memory loading file: %s", file_path);
        return ESP_ERR_NO_MEM;
    }

    // Ler conteúdo
    size_t read_size = fread(*content, 1, file_size, file);
    fclose(file);

    if (read_size != file_size) {
        free(*content);
        *content = NULL;
        ESP_LOGE(TAG, "Failed to read complete file: %s", file_path);
        return ESP_FAIL;
    }

    *size = file_size;

    // Adicionar ao cache se habilitado
    if (g_static_ctx.caching_enabled) {
        wsm_static_cache_entry_t *cache_entry = wsm_static_cache_get_free_slot();
        if (cache_entry) {
            strncpy(cache_entry->file_path, file_path, sizeof(cache_entry->file_path) - 1);
            cache_entry->file_path[sizeof(cache_entry->file_path) - 1] = '\0';
            
            cache_entry->content = malloc(file_size);
            if (cache_entry->content) {
                memcpy(cache_entry->content, *content, file_size);
                cache_entry->size = file_size;
                cache_entry->last_modified = wsm_static_get_file_mtime(file_path);
                cache_entry->compressed = false;
                wsm_static_generate_etag(file_path, cache_entry->etag, sizeof(cache_entry->etag));
                cache_entry->valid = true;
                ESP_LOGD(TAG, "File cached: %s", file_path);
            }
        }
    }

    ESP_LOGD(TAG, "File loaded: %s (%zu bytes)", file_path, *size);
    return ESP_OK;
}

// =============================================================================
// HANDLERS DE ARQUIVOS ESTÁTICOS
// =============================================================================

esp_err_t wsm_static_serve_file(httpd_req_t *req, const char *file_path, bool enable_caching)
{
    if (!req || !file_path) {
        return ESP_ERR_INVALID_ARG;
    }

    // Verificar se arquivo existe
    if (!wsm_static_file_exists(file_path)) {
        ESP_LOGW(TAG, "File not found: %s", file_path);
        return httpd_resp_send_404(req);
    }

    // Gerar ETag se caching habilitado
    char etag[64];
    bool has_etag = false;
    if (enable_caching) {
        if (wsm_static_generate_etag(file_path, etag, sizeof(etag)) == ESP_OK) {
            has_etag = true;
            
            // Verificar If-None-Match
            if (wsm_static_check_if_none_match(req, etag)) {
                httpd_resp_set_status(req, "304 Not Modified");
                httpd_resp_set_hdr(req, "ETag", etag);
                return httpd_resp_send(req, NULL, 0);
            }
        }
    }

    // Carregar arquivo
    char *content = NULL;
    size_t size = 0;
    esp_err_t ret = wsm_static_load_file(file_path, &content, &size);
    if (ret != ESP_OK) {
        return httpd_resp_send_500(req);
    }

    // Definir headers
    const char *mime_type = wsm_mime_get_type(file_path);
    httpd_resp_set_type(req, mime_type);

    if (enable_caching) {
        if (has_etag) {
            httpd_resp_set_hdr(req, "ETag", etag);
        }
        httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=3600");
    } else {
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    }

    // Enviar resposta
    ret = httpd_resp_send(req, content, size);
    
    free(content);
    return ret;
}

esp_err_t wsm_static_file_handler(httpd_req_t *req)
{
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *uri = req->uri;
    
    // Procurar handler apropriado
    for (size_t i = 0; i < g_static_ctx.handler_count; i++) {
        wsm_static_handler_config_t *config = &g_static_ctx.handlers[i];
        
        // Verificar se URI corresponde ao prefixo
        if (strncmp(uri, config->uri_prefix, strlen(config->uri_prefix)) == 0) {
            // Construir caminho do arquivo
            const char *file_path_part = uri + strlen(config->uri_prefix);
            if (file_path_part[0] == '/') {
                file_path_part++;
            }
            
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s", 
                     config->file_path_prefix, file_path_part);
            
            return wsm_static_serve_file(req, full_path, config->enable_caching);
        }
    }

    // Handler padrão - mapear diretamente
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s%s", 
             g_wsm_ctx.config.static_files_path, uri);
    
    return wsm_static_serve_file(req, file_path, true);
}

// =============================================================================
// HANDLERS ESPECÍFICOS
// =============================================================================

esp_err_t wsm_css_handler(httpd_req_t *req)
{
    return wsm_static_file_handler(req);
}

esp_err_t wsm_js_handler(httpd_req_t *req)
{
    return wsm_static_file_handler(req);
}

esp_err_t wsm_image_handler(httpd_req_t *req)
{
    return wsm_static_file_handler(req);
}

esp_err_t wsm_font_handler(httpd_req_t *req)
{
    return wsm_static_file_handler(req);
}

// =============================================================================
// FUNÇÕES DE REGISTRO
// =============================================================================

esp_err_t wsm_static_register_handler(const wsm_static_handler_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    if (g_static_ctx.handler_count >= WSM_MAX_STATIC_HANDLERS) {
        ESP_LOGE(TAG, "Maximum static handlers reached");
        return ESP_ERR_NO_MEM;
    }

    g_static_ctx.handlers[g_static_ctx.handler_count] = *config;
    g_static_ctx.handler_count++;

    ESP_LOGI(TAG, "Static handler registered: %s -> %s", 
             config->uri_prefix, config->file_path_prefix);
    return ESP_OK;
}

esp_err_t wsm_register_default_static_handlers(const char *base_path)
{
    if (!base_path) {
        return ESP_ERR_INVALID_ARG;
    }

    // Registrar handlers básicos
    wsm_route_config_t routes[] = {
        {"/css/*", HTTP_GET, wsm_css_handler, WSM_USER_LEVEL_NONE, NULL, false},
        {"/js/*", HTTP_GET, wsm_js_handler, WSM_USER_LEVEL_NONE, NULL, false},
        {"/images/*", HTTP_GET, wsm_image_handler, WSM_USER_LEVEL_NONE, NULL, false},
        {"/fonts/*", HTTP_GET, wsm_font_handler, WSM_USER_LEVEL_NONE, NULL, false}
    };

    esp_err_t ret = wsm_register_routes(routes, sizeof(routes) / sizeof(routes[0]));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register static file routes");
        return ret;
    }

    ESP_LOGI(TAG, "Default static handlers registered");
    return ESP_OK;
}