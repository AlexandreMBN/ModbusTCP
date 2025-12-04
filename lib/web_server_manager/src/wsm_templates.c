/**
 * @file wsm_templates.c
 * @brief Sistema de templates do Web Server Manager
 */

#include "wsm_templates.h"
#include "web_server_manager.h"
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <ctype.h>

static const char *TAG = "WSM_TEMPLATES";

// =============================================================================
// VARIÁVEIS GLOBAIS INTERNAS
// =============================================================================

static wsm_template_engine_t g_template_engine = {0};
static bool g_template_engine_initialized = false;

// =============================================================================
// FUNÇÕES DE INICIALIZAÇÃO DO ENGINE
// =============================================================================

esp_err_t wsm_template_engine_init(const char *base_path)
{
    if (g_template_engine_initialized) {
        ESP_LOGW(TAG, "Template engine already initialized");
        return ESP_OK;
    }

    if (!base_path) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing template engine with base path: %s", base_path);

    memset(&g_template_engine, 0, sizeof(wsm_template_engine_t));
    strncpy(g_template_engine.base_path, base_path, sizeof(g_template_engine.base_path) - 1);
    g_template_engine.caching_enabled = true;
    g_template_engine_initialized = true;

    ESP_LOGI(TAG, "Template engine initialized successfully");
    return ESP_OK;
}

esp_err_t wsm_template_engine_deinit(void)
{
    if (!g_template_engine_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing template engine...");

    // Limpar cache
    wsm_template_engine_clear_cache();
    
    memset(&g_template_engine, 0, sizeof(wsm_template_engine_t));
    g_template_engine_initialized = false;

    ESP_LOGI(TAG, "Template engine deinitialized");
    return ESP_OK;
}

void wsm_template_engine_set_caching(bool enabled)
{
    g_template_engine.caching_enabled = enabled;
    if (!enabled) {
        wsm_template_engine_clear_cache();
    }
    ESP_LOGI(TAG, "Template caching %s", enabled ? "enabled" : "disabled");
}

esp_err_t wsm_template_engine_clear_cache(void)
{
    for (int i = 0; i < WSM_TEMPLATE_CACHE_SIZE; i++) {
        wsm_template_cache_entry_t *entry = &g_template_engine.cache[i];
        if (entry->valid && entry->content) {
            free(entry->content);
            entry->content = NULL;
        }
        entry->valid = false;
    }

    g_template_engine.cache_hits = 0;
    g_template_engine.cache_misses = 0;

    ESP_LOGI(TAG, "Template cache cleared");
    return ESP_OK;
}

esp_err_t wsm_template_engine_get_stats(size_t *hits, size_t *misses)
{
    if (!hits || !misses) {
        return ESP_ERR_INVALID_ARG;
    }

    *hits = g_template_engine.cache_hits;
    *misses = g_template_engine.cache_misses;
    return ESP_OK;
}

// =============================================================================
// FUNÇÕES DE CACHE
// =============================================================================

static wsm_template_cache_entry_t *wsm_template_cache_find(const char *path)
{
    for (int i = 0; i < WSM_TEMPLATE_CACHE_SIZE; i++) {
        wsm_template_cache_entry_t *entry = &g_template_engine.cache[i];
        if (entry->valid && strcmp(entry->path, path) == 0) {
            return entry;
        }
    }
    return NULL;
}

static wsm_template_cache_entry_t *wsm_template_cache_get_free_slot(void)
{
    // Procurar slot vazio
    for (int i = 0; i < WSM_TEMPLATE_CACHE_SIZE; i++) {
        wsm_template_cache_entry_t *entry = &g_template_engine.cache[i];
        if (!entry->valid) {
            return entry;
        }
    }

    // Se não houver slots vazios, usar o mais antigo (LRU simples)
    wsm_template_cache_entry_t *oldest = &g_template_engine.cache[0];
    for (int i = 1; i < WSM_TEMPLATE_CACHE_SIZE; i++) {
        wsm_template_cache_entry_t *entry = &g_template_engine.cache[i];
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

static uint32_t wsm_template_get_file_mtime(const char *file_path)
{
    struct stat st;
    if (stat(file_path, &st) == 0) {
        return (uint32_t)st.st_mtime;
    }
    return 0;
}

// =============================================================================
// FUNÇÕES DE CARREGAMENTO DE TEMPLATE
// =============================================================================

esp_err_t wsm_template_load(const char *template_path, char **content)
{
    if (!template_path || !content) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!g_template_engine_initialized) {
        ESP_LOGE(TAG, "Template engine not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Construir caminho completo
    char full_path[512];
    if (template_path[0] == '/') {
        strncpy(full_path, template_path, sizeof(full_path) - 1);
    } else {
        snprintf(full_path, sizeof(full_path), "%s/%s", 
                 g_template_engine.base_path, template_path);
    }
    full_path[sizeof(full_path) - 1] = '\0';

    // Verificar cache se habilitado
    if (g_template_engine.caching_enabled) {
        wsm_template_cache_entry_t *cached = wsm_template_cache_find(full_path);
        if (cached) {
            uint32_t file_mtime = wsm_template_get_file_mtime(full_path);
            if (file_mtime <= cached->last_modified) {
                *content = malloc(cached->size + 1);
                if (*content) {
                    memcpy(*content, cached->content, cached->size);
                    (*content)[cached->size] = '\0';
                    g_template_engine.cache_hits++;
                    ESP_LOGD(TAG, "Template cache hit: %s", template_path);
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
    g_template_engine.cache_misses++;

    FILE *file = fopen(full_path, "r");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open template: %s", full_path);
        return ESP_FAIL;
    }

    // Obter tamanho do arquivo
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);

    if (file_size <= 0) {
        fclose(file);
        ESP_LOGE(TAG, "Template file is empty: %s", full_path);
        return ESP_FAIL;
    }

    // Alocar memória
    *content = malloc(file_size + 1);
    if (!*content) {
        fclose(file);
        ESP_LOGE(TAG, "Out of memory loading template: %s", full_path);
        return ESP_ERR_NO_MEM;
    }

    // Ler conteúdo
    size_t read_size = fread(*content, 1, file_size, file);
    fclose(file);

    if (read_size != file_size) {
        free(*content);
        *content = NULL;
        ESP_LOGE(TAG, "Failed to read complete template: %s", full_path);
        return ESP_FAIL;
    }

    (*content)[file_size] = '\0';

    // Adicionar ao cache se habilitado
    if (g_template_engine.caching_enabled) {
        wsm_template_cache_entry_t *cache_entry = wsm_template_cache_get_free_slot();
        if (cache_entry) {
            strncpy(cache_entry->path, full_path, sizeof(cache_entry->path) - 1);
            cache_entry->path[sizeof(cache_entry->path) - 1] = '\0';
            
            cache_entry->content = malloc(file_size + 1);
            if (cache_entry->content) {
                memcpy(cache_entry->content, *content, file_size + 1);
                cache_entry->size = file_size;
                cache_entry->last_modified = wsm_template_get_file_mtime(full_path);
                cache_entry->valid = true;
                ESP_LOGD(TAG, "Template cached: %s", template_path);
            }
        }
    }

    ESP_LOGD(TAG, "Template loaded: %s (%ld bytes)", template_path, file_size);
    return ESP_OK;
}

// =============================================================================
// FUNÇÕES DE SUBSTITUIÇÃO DE PLACEHOLDERS
// =============================================================================

esp_err_t wsm_template_replace_placeholder(const char *template,
                                           const char *placeholder,
                                           const char *value,
                                           char **output)
{
    if (!template || !placeholder || !value || !output) {
        return ESP_ERR_INVALID_ARG;
    }

    char search_pattern[128];
    snprintf(search_pattern, sizeof(search_pattern), "%s%s%s", 
             WSM_TEMPLATE_PLACEHOLDER_START, placeholder, WSM_TEMPLATE_PLACEHOLDER_END);

    // Contar ocorrências
    size_t count = 0;
    const char *pos = template;
    size_t pattern_len = strlen(search_pattern);
    
    while ((pos = strstr(pos, search_pattern)) != NULL) {
        count++;
        pos += pattern_len;
    }

    if (count == 0) {
        // Nenhuma ocorrência, retorna cópia do template
        *output = malloc(strlen(template) + 1);
        if (*output) {
            strcpy(*output, template);
            return ESP_OK;
        }
        return ESP_ERR_NO_MEM;
    }

    // Calcular novo tamanho
    size_t value_len = strlen(value);
    size_t result_len = strlen(template) + count * (value_len - pattern_len) + 1;

    *output = malloc(result_len);
    if (!*output) {
        return ESP_ERR_NO_MEM;
    }

    // Realizar substituições
    const char *src = template;
    char *dst = *output;
    
    while ((pos = strstr(src, search_pattern)) != NULL) {
        // Copiar até o placeholder
        size_t copy_len = pos - src;
        memcpy(dst, src, copy_len);
        dst += copy_len;

        // Copiar valor de substituição
        strcpy(dst, value);
        dst += value_len;

        // Mover para após o placeholder
        src = pos + pattern_len;
    }
    
    // Copiar o resto
    strcpy(dst, src);

    return ESP_OK;
}

esp_err_t wsm_template_apply_substitutions(const char *template_content,
                                           const wsm_template_context_t *context,
                                           char **output)
{
    if (!template_content || !context || !output) {
        return ESP_ERR_INVALID_ARG;
    }

    char *current = malloc(strlen(template_content) + 1);
    if (!current) {
        return ESP_ERR_NO_MEM;
    }
    strcpy(current, template_content);

    // Aplicar cada substituição
    for (size_t i = 0; i < context->count; i++) {
        char *new_template = NULL;
        esp_err_t ret = wsm_template_replace_placeholder(current, 
                                                         context->substitutions[i].key,
                                                         context->substitutions[i].value,
                                                         &new_template);
        if (ret == ESP_OK && new_template) {
            free(current);
            current = new_template;
        } else if (ret != ESP_OK) {
            free(current);
            return ret;
        }
    }

    *output = current;
    return ESP_OK;
}

size_t wsm_template_count_placeholders(const char *template_content,
                                       const char *placeholder)
{
    if (!template_content || !placeholder) {
        return 0;
    }

    char search_pattern[128];
    snprintf(search_pattern, sizeof(search_pattern), "%s%s%s", 
             WSM_TEMPLATE_PLACEHOLDER_START, placeholder, WSM_TEMPLATE_PLACEHOLDER_END);

    size_t count = 0;
    const char *pos = template_content;
    size_t pattern_len = strlen(search_pattern);
    
    while ((pos = strstr(pos, search_pattern)) != NULL) {
        count++;
        pos += pattern_len;
    }

    return count;
}

// =============================================================================
// FUNÇÕES DE CONTEXTO DE TEMPLATE
// =============================================================================

void wsm_template_context_init(wsm_template_context_t *context)
{
    if (context) {
        memset(context, 0, sizeof(wsm_template_context_t));
    }
}

esp_err_t wsm_template_add_substitution(wsm_template_context_t *context,
                                        const char *key, const char *value)
{
    if (!context || !key || !value) {
        return ESP_ERR_INVALID_ARG;
    }

    if (context->count >= WEB_SERVER_MAX_SUBSTITUTIONS) {
        ESP_LOGE(TAG, "Maximum substitutions reached");
        return ESP_ERR_NO_MEM;
    }

    wsm_template_substitution_t *sub = &context->substitutions[context->count];
    strncpy(sub->key, key, sizeof(sub->key) - 1);
    sub->key[sizeof(sub->key) - 1] = '\0';
    
    strncpy(sub->value, value, sizeof(sub->value) - 1);
    sub->value[sizeof(sub->value) - 1] = '\0';

    context->count++;
    return ESP_OK;
}

// =============================================================================
// FUNÇÕES PÚBLICAS DE RENDERIZAÇÃO
// =============================================================================

esp_err_t wsm_render_template(const char *template_path,
                              const wsm_template_context_t *context,
                              char **output)
{
    if (!template_path || !output) {
        return ESP_ERR_INVALID_ARG;
    }

    // Carregar template
    char *template_content = NULL;
    esp_err_t ret = wsm_template_load(template_path, &template_content);
    if (ret != ESP_OK) {
        return ret;
    }

    // Aplicar substituições se contexto fornecido
    if (context && context->count > 0) {
        ret = wsm_template_apply_substitutions(template_content, context, output);
        free(template_content);
    } else {
        *output = template_content; // Usar template sem modificações
        ret = ESP_OK;
    }

    return ret;
}

esp_err_t wsm_respond_with_template(httpd_req_t *req, const char *template_path,
                                    const wsm_template_context_t *context)
{
    if (!req || !template_path) {
        return ESP_ERR_INVALID_ARG;
    }

    char *rendered_content = NULL;
    esp_err_t ret = wsm_render_template(template_path, context, &rendered_content);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to render template: %s", template_path);
        return ret;
    }

    httpd_resp_set_type(req, "text/html");
    ret = httpd_resp_send(req, rendered_content, strlen(rendered_content));
    
    free(rendered_content);
    return ret;
}

// =============================================================================
// HELPERS PARA CONTEXTOS COMUNS
// =============================================================================

esp_err_t wsm_template_add_system_info(wsm_template_context_t *context)
{
    if (!context) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    // Informações do chip
    wsm_template_add_substitution(context, "CHIP_MODEL", "ESP32");
    
    char revision_str[8];
    snprintf(revision_str, sizeof(revision_str), "%d", chip_info.revision);
    wsm_template_add_substitution(context, "CHIP_REVISION", revision_str);

    char cores_str[8];
    snprintf(cores_str, sizeof(cores_str), "%d", chip_info.cores);
    wsm_template_add_substitution(context, "CHIP_CORES", cores_str);

    // Informações de memória
    uint32_t free_heap = esp_get_free_heap_size();
    char heap_str[16];
    snprintf(heap_str, sizeof(heap_str), "%lu", free_heap);
    wsm_template_add_substitution(context, "FREE_HEAP", heap_str);

    return ESP_OK;
}

esp_err_t wsm_template_add_timestamp(wsm_template_context_t *context)
{
    if (!context) {
        return ESP_ERR_INVALID_ARG;
    }

    uint64_t timestamp = esp_timer_get_time() / 1000000;
    char timestamp_str[32];
    snprintf(timestamp_str, sizeof(timestamp_str), "%llu", timestamp);
    
    wsm_template_add_substitution(context, "TIMESTAMP", timestamp_str);

    return ESP_OK;
}

esp_err_t wsm_template_validate(const char *template_content)
{
    if (!template_content) {
        return ESP_ERR_INVALID_ARG;
    }

    // Verificação básica de sintaxe de placeholders
    const char *pos = template_content;
    int placeholder_depth = 0;
    
    while (*pos) {
        if (strncmp(pos, WSM_TEMPLATE_PLACEHOLDER_START, 
                    strlen(WSM_TEMPLATE_PLACEHOLDER_START)) == 0) {
            placeholder_depth++;
            pos += strlen(WSM_TEMPLATE_PLACEHOLDER_START);
        } else if (strncmp(pos, WSM_TEMPLATE_PLACEHOLDER_END, 
                           strlen(WSM_TEMPLATE_PLACEHOLDER_END)) == 0) {
            placeholder_depth--;
            if (placeholder_depth < 0) {
                ESP_LOGE(TAG, "Template validation error: unmatched placeholder end");
                return ESP_ERR_INVALID_ARG;
            }
            pos += strlen(WSM_TEMPLATE_PLACEHOLDER_END);
        } else {
            pos++;
        }
    }

    if (placeholder_depth != 0) {
        ESP_LOGE(TAG, "Template validation error: unmatched placeholder start");
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}