#ifndef WSM_STATIC_FILES_H
#define WSM_STATIC_FILES_H

#include "web_server_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// CONFIGURAÇÕES DE ARQUIVOS ESTÁTICOS
// =============================================================================

#define WSM_MAX_STATIC_HANDLERS     16
#define WSM_MAX_MIME_TYPES          32
#define WSM_STATIC_CACHE_SIZE       16

// =============================================================================
// ESTRUTURAS
// =============================================================================

/**
 * @brief Entrada de tipo MIME
 */
typedef struct {
    char extension[16];                     /**< Extensão do arquivo */
    char mime_type[64];                     /**< Tipo MIME */
} wsm_mime_entry_t;

/**
 * @brief Configuração de handler de arquivos estáticos
 */
typedef struct {
    char uri_prefix[128];                   /**< Prefixo URI */
    char file_path_prefix[256];             /**< Prefixo do caminho do arquivo */
    bool enable_caching;                    /**< Habilitar cache */
    uint32_t cache_max_age;                 /**< Idade máxima do cache em segundos */
    bool enable_compression;                /**< Habilitar compressão */
    bool directory_listing;                 /**< Permitir listagem de diretórios */
} wsm_static_handler_config_t;

/**
 * @brief Cache de arquivo estático
 */
typedef struct {
    char file_path[256];                    /**< Caminho do arquivo */
    char *content;                          /**< Conteúdo do arquivo */
    size_t size;                            /**< Tamanho do conteúdo */
    uint32_t last_modified;                 /**< Timestamp da última modificação */
    char etag[64];                          /**< ETag do arquivo */
    bool compressed;                        /**< Conteúdo comprimido */
    bool valid;                             /**< Cache válido */
} wsm_static_cache_entry_t;

// =============================================================================
// FUNÇÕES DE ARQUIVOS ESTÁTICOS
// =============================================================================

/**
 * @brief Inicializar sistema de arquivos estáticos
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_static_init(void);

/**
 * @brief Desinicializar sistema de arquivos estáticos
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_static_deinit(void);

/**
 * @brief Registrar handler de arquivos estáticos
 * @param config Configuração do handler
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_static_register_handler(const wsm_static_handler_config_t *config);

/**
 * @brief Handler genérico para arquivos estáticos
 * @param req Requisição HTTP
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_static_file_handler(httpd_req_t *req);

/**
 * @brief Servir arquivo específico
 * @param req Requisição HTTP
 * @param file_path Caminho completo do arquivo
 * @param enable_caching Habilitar headers de cache
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_static_serve_file(httpd_req_t *req, const char *file_path, bool enable_caching);

// =============================================================================
// FUNÇÕES DE MIME TYPES
// =============================================================================

/**
 * @brief Inicializar tipos MIME padrão
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_mime_types_init(void);

/**
 * @brief Adicionar tipo MIME customizado
 * @param extension Extensão do arquivo (com ou sem ponto)
 * @param mime_type Tipo MIME
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_mime_add_type(const char *extension, const char *mime_type);

/**
 * @brief Obter tipo MIME para arquivo
 * @param file_path Caminho do arquivo
 * @return String com tipo MIME (nunca NULL)
 */
const char *wsm_mime_get_type(const char *file_path);

// =============================================================================
// FUNÇÕES DE CACHE
// =============================================================================

/**
 * @brief Habilitar/desabilitar cache de arquivos estáticos
 * @param enabled Estado desejado
 */
void wsm_static_set_caching_enabled(bool enabled);

/**
 * @brief Limpar cache de arquivos estáticos
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_static_clear_cache(void);

/**
 * @brief Obter estatísticas do cache
 * @param hits Ponteiro para armazenar hits
 * @param misses Ponteiro para armazenar misses
 * @param entries Ponteiro para armazenar número de entradas
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_static_get_cache_stats(size_t *hits, size_t *misses, size_t *entries);

// =============================================================================
// FUNÇÕES UTILITÁRIAS
// =============================================================================

/**
 * @brief Verificar se arquivo existe
 * @param file_path Caminho do arquivo
 * @return true se existe
 */
bool wsm_static_file_exists(const char *file_path);

/**
 * @brief Obter tamanho do arquivo
 * @param file_path Caminho do arquivo
 * @return Tamanho em bytes, ou -1 em caso de erro
 */
long wsm_static_get_file_size(const char *file_path);

/**
 * @brief Obter timestamp de modificação do arquivo
 * @param file_path Caminho do arquivo
 * @return Timestamp, ou 0 em caso de erro
 */
uint32_t wsm_static_get_file_mtime(const char *file_path);

/**
 * @brief Gerar ETag para arquivo
 * @param file_path Caminho do arquivo
 * @param etag Buffer para armazenar ETag
 * @param etag_size Tamanho do buffer
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_static_generate_etag(const char *file_path, char *etag, size_t etag_size);

/**
 * @brief Verificar se requisição tem If-None-Match com ETag válido
 * @param req Requisição HTTP
 * @param etag ETag para comparar
 * @return true se ETags coincidem
 */
bool wsm_static_check_if_none_match(httpd_req_t *req, const char *etag);

// =============================================================================
// HANDLERS ESPECÍFICOS
// =============================================================================

/**
 * @brief Handler para arquivos CSS
 * @param req Requisição HTTP
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_css_handler(httpd_req_t *req);

/**
 * @brief Handler para arquivos JavaScript
 * @param req Requisição HTTP
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_js_handler(httpd_req_t *req);

/**
 * @brief Handler para imagens
 * @param req Requisição HTTP
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_image_handler(httpd_req_t *req);

/**
 * @brief Handler para fontes
 * @param req Requisição HTTP
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_font_handler(httpd_req_t *req);

/**
 * @brief Registrar todos os handlers de arquivos estáticos padrão
 * @param base_path Caminho base dos arquivos estáticos
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_register_default_static_handlers(const char *base_path);

#ifdef __cplusplus
}
#endif

#endif /* WSM_STATIC_FILES_H */