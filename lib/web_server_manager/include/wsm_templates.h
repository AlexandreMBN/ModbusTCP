#ifndef WSM_TEMPLATES_H
#define WSM_TEMPLATES_H

#include "web_server_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// CONFIGURAÇÕES DE TEMPLATE
// =============================================================================

#define WSM_TEMPLATE_PLACEHOLDER_START  "{{"
#define WSM_TEMPLATE_PLACEHOLDER_END    "}}"
#define WSM_TEMPLATE_MAX_INCLUDE_DEPTH  5
#define WSM_TEMPLATE_CACHE_SIZE         8

// =============================================================================
// ESTRUTURAS DE TEMPLATE
// =============================================================================

/**
 * @brief Cache de template
 */
typedef struct {
    char path[256];                         /**< Caminho do template */
    char *content;                          /**< Conteúdo do template */
    size_t size;                            /**< Tamanho do conteúdo */
    uint32_t last_modified;                 /**< Timestamp da última modificação */
    bool valid;                             /**< Cache válido */
} wsm_template_cache_entry_t;

/**
 * @brief Engine de templates
 */
typedef struct {
    wsm_template_cache_entry_t cache[WSM_TEMPLATE_CACHE_SIZE];
    size_t cache_hits;                      /**< Hits do cache */
    size_t cache_misses;                    /**< Misses do cache */
    bool caching_enabled;                   /**< Cache habilitado */
    char base_path[256];                    /**< Caminho base dos templates */
} wsm_template_engine_t;

// =============================================================================
// FUNÇÕES DE TEMPLATE ENGINE
// =============================================================================

/**
 * @brief Inicializar engine de templates
 * @param base_path Caminho base dos templates
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_template_engine_init(const char *base_path);

/**
 * @brief Desinicializar engine de templates
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_template_engine_deinit(void);

/**
 * @brief Habilitar/desabilitar cache de templates
 * @param enabled Estado desejado
 */
void wsm_template_engine_set_caching(bool enabled);

/**
 * @brief Limpar cache de templates
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_template_engine_clear_cache(void);

/**
 * @brief Obter estatísticas do cache
 * @param hits Ponteiro para armazenar hits
 * @param misses Ponteiro para armazenar misses
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_template_engine_get_stats(size_t *hits, size_t *misses);

// =============================================================================
// FUNÇÕES DE PROCESSAMENTO DE TEMPLATE
// =============================================================================

/**
 * @brief Carregar template do arquivo
 * @param template_path Caminho do template
 * @param content Ponteiro para armazenar conteúdo (será alocado)
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_template_load(const char *template_path, char **content);

/**
 * @brief Aplicar substituições em template
 * @param template_content Conteúdo do template
 * @param context Contexto com substituições
 * @param output Ponteiro para resultado (será alocado)
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_template_apply_substitutions(const char *template_content,
                                           const wsm_template_context_t *context,
                                           char **output);

/**
 * @brief Processar includes em template
 * @param template_content Conteúdo do template
 * @param base_path Caminho base para includes
 * @param output Ponteiro para resultado (será alocado)
 * @param depth Profundidade atual de includes (uso interno)
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_template_process_includes(const char *template_content,
                                        const char *base_path,
                                        char **output,
                                        int depth);

/**
 * @brief Validar sintaxe de template
 * @param template_content Conteúdo do template
 * @return ESP_OK se válido, ESP_ERR_INVALID_ARG caso contrário
 */
esp_err_t wsm_template_validate(const char *template_content);

// =============================================================================
// FUNÇÕES UTILITÁRIAS DE TEMPLATE
// =============================================================================

/**
 * @brief Substituir placeholders em string
 * @param template String template
 * @param placeholder Nome do placeholder (sem chaves)
 * @param value Valor para substituição
 * @param output Ponteiro para resultado (será alocado)
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_template_replace_placeholder(const char *template,
                                           const char *placeholder,
                                           const char *value,
                                           char **output);

/**
 * @brief Contar placeholders em template
 * @param template_content Conteúdo do template
 * @param placeholder Nome do placeholder
 * @return Número de ocorrências
 */
size_t wsm_template_count_placeholders(const char *template_content,
                                       const char *placeholder);

/**
 * @brief Listar todos os placeholders em template
 * @param template_content Conteúdo do template
 * @param placeholders Array para armazenar placeholders encontrados
 * @param max_placeholders Tamanho máximo do array
 * @return Número de placeholders encontrados
 */
size_t wsm_template_list_placeholders(const char *template_content,
                                      char placeholders[][64],
                                      size_t max_placeholders);

// =============================================================================
// HELPERS PARA CONTEXTOS COMUNS
// =============================================================================

/**
 * @brief Adicionar informações do sistema ao contexto
 * @param context Contexto do template
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_template_add_system_info(wsm_template_context_t *context);

/**
 * @brief Adicionar informações de rede ao contexto
 * @param context Contexto do template
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_template_add_network_info(wsm_template_context_t *context);

/**
 * @brief Adicionar informações de usuário ao contexto
 * @param context Contexto do template
 * @param req Requisição HTTP
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_template_add_user_info(wsm_template_context_t *context, httpd_req_t *req);

/**
 * @brief Adicionar timestamp atual ao contexto
 * @param context Contexto do template
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_template_add_timestamp(wsm_template_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* WSM_TEMPLATES_H */