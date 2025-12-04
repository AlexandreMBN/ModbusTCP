#ifndef SPIFFS_FILE_MANAGER_H
#define SPIFFS_FILE_MANAGER_H

#include <esp_err.h>
#include <esp_http_server.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief SPIFFS File Manager Library para ESP32
 * 
 * Biblioteca completa para gerenciamento de arquivos web em SPIFFS,
 * incluindo:
 * - Carregamento automático de arquivos HTML, CSS, JS
 * - Handlers prontos para webserver
 * - Detecção automática de MIME types
 * - Sistema de cache e otimização
 * - Templates com placeholders
 * - Compressão de assets (futuramente)
 */

// Constantes de configuração
#define SPIFFS_MANAGER_MAX_PATH_LEN 512
#define SPIFFS_MANAGER_MAX_FILE_SIZE (256 * 1024)  // 256KB
#define SPIFFS_MANAGER_MAX_PLACEHOLDERS 20
#define SPIFFS_MANAGER_DEFAULT_BASE_PATH "/spiffs"

// Estrutura para configuração do manager
typedef struct {
    const char *base_path;              // Caminho base SPIFFS (padrão: "/spiffs")
    const char *default_index;          // Arquivo index padrão (padrão: "index.html")
    bool enable_cache;                  // Habilitar cache em memória
    bool enable_compression;            // Habilitar compressão (futuro)
    bool enable_development_headers;    // Headers anti-cache para desenvolvimento
    size_t max_file_size;              // Tamanho máximo de arquivo
    uint8_t max_open_files;            // Número máximo de arquivos abertos
} spiffs_manager_config_t;

// Estrutura para substituições de template
typedef struct {
    const char *placeholder;           // Nome do placeholder (sem {{}} )
    const char *value;                 // Valor para substituir
} spiffs_template_var_t;

// Tipos de resultado para operações
typedef enum {
    SPIFFS_MANAGER_OK = 0,
    SPIFFS_MANAGER_ERROR_FILE_NOT_FOUND,
    SPIFFS_MANAGER_ERROR_OUT_OF_MEMORY,
    SPIFFS_MANAGER_ERROR_FILE_TOO_LARGE,
    SPIFFS_MANAGER_ERROR_INVALID_PATH,
    SPIFFS_MANAGER_ERROR_SPIFFS_NOT_MOUNTED,
    SPIFFS_MANAGER_ERROR_INVALID_TEMPLATE
} spiffs_manager_result_t;

// Callback para processar arquivos antes de servir
typedef esp_err_t (*spiffs_file_preprocessor_t)(const char *filepath, char **content, size_t *content_length);

/**
 * @brief Obtém configuração padrão
 * 
 * @param config Ponteiro para estrutura de configuração
 */
void spiffs_manager_get_default_config(spiffs_manager_config_t *config);

/**
 * @brief Inicializa o SPIFFS File Manager
 * 
 * @param config Configuração (NULL para usar padrões)
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t spiffs_manager_init(const spiffs_manager_config_t *config);

/**
 * @brief Deinicializa o manager e libera recursos
 * 
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t spiffs_manager_deinit(void);

/**
 * @brief Verifica se o manager está inicializado
 * 
 * @return true se inicializado
 */
bool spiffs_manager_is_initialized(void);

// ============================================================================
// OPERAÇÕES DE ARQUIVO
// ============================================================================

/**
 * @brief Carrega conteúdo de arquivo do SPIFFS
 * 
 * @param filepath Caminho do arquivo (relativo ao base_path)
 * @param content Ponteiro para receber conteúdo (deve ser liberado com free())
 * @param content_length Ponteiro para receber tamanho (opcional)
 * @return spiffs_manager_result_t Resultado da operação
 */
spiffs_manager_result_t spiffs_manager_load_file(const char *filepath, char **content, size_t *content_length);

/**
 * @brief Verifica se arquivo existe
 * 
 * @param filepath Caminho do arquivo
 * @return true se arquivo existe
 */
bool spiffs_manager_file_exists(const char *filepath);

/**
 * @brief Obtém tamanho de arquivo
 * 
 * @param filepath Caminho do arquivo
 * @param size Ponteiro para receber tamanho
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t spiffs_manager_get_file_size(const char *filepath, size_t *size);

/**
 * @brief Força remontagem do SPIFFS
 * 
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t spiffs_manager_remount_spiffs(void);

// ============================================================================
// MIME TYPES E HEADERS HTTP
// ============================================================================

/**
 * @brief Obtém tipo MIME baseado na extensão do arquivo
 * 
 * @param filepath Caminho do arquivo
 * @return const char* Tipo MIME
 */
const char* spiffs_manager_get_mime_type(const char *filepath);

/**
 * @brief Registra tipo MIME customizado
 * 
 * @param extension Extensão do arquivo (ex: ".pdf")
 * @param mime_type Tipo MIME (ex: "application/pdf")
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t spiffs_manager_register_mime_type(const char *extension, const char *mime_type);

/**
 * @brief Define headers HTTP apropriados baseado no tipo de arquivo
 * 
 * @param req Requisição HTTP
 * @param filepath Caminho do arquivo
 * @param enable_cache Habilitar cache (false para desenvolvimento)
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t spiffs_manager_set_http_headers(httpd_req_t *req, const char *filepath, bool enable_cache);

// ============================================================================
// SISTEMA DE TEMPLATES
// ============================================================================

/**
 * @brief Processa template com substituições de variáveis
 * 
 * @param template_content Conteúdo do template
 * @param variables Array de variáveis para substituição
 * @param var_count Número de variáveis
 * @param processed_content Ponteiro para receber conteúdo processado (deve ser liberado)
 * @return spiffs_manager_result_t Resultado da operação
 */
spiffs_manager_result_t spiffs_manager_process_template(const char *template_content,
                                                       const spiffs_template_var_t *variables,
                                                       size_t var_count,
                                                       char **processed_content);

/**
 * @brief Carrega e processa template de arquivo
 * 
 * @param template_path Caminho do arquivo template
 * @param variables Array de variáveis
 * @param var_count Número de variáveis
 * @param processed_content Ponteiro para receber resultado
 * @return spiffs_manager_result_t Resultado da operação
 */
spiffs_manager_result_t spiffs_manager_load_and_process_template(const char *template_path,
                                                                const spiffs_template_var_t *variables,
                                                                size_t var_count,
                                                                char **processed_content);

// ============================================================================
// HANDLERS HTTP PRONTOS PARA USO
// ============================================================================

/**
 * @brief Handler genérico para arquivos estáticos
 * 
 * Serve qualquer arquivo do SPIFFS baseado na URI.
 * Detecta automaticamente tipo MIME e define headers apropriados.
 * 
 * @param req Requisição HTTP
 * @return esp_err_t ESP_OK se sucesso, ESP_FAIL se arquivo não encontrado
 */
esp_err_t spiffs_manager_static_handler(httpd_req_t *req);

/**
 * @brief Handler específico para arquivos CSS
 * 
 * @param req Requisição HTTP  
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t spiffs_manager_css_handler(httpd_req_t *req);

/**
 * @brief Handler específico para arquivos JavaScript
 * 
 * @param req Requisição HTTP
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t spiffs_manager_js_handler(httpd_req_t *req);

/**
 * @brief Handler específico para arquivos HTML
 * 
 * @param req Requisição HTTP
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t spiffs_manager_html_handler(httpd_req_t *req);

/**
 * @brief Handler para página inicial (/)
 * 
 * Serve automaticamente o arquivo index configurado.
 * 
 * @param req Requisição HTTP
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t spiffs_manager_index_handler(httpd_req_t *req);

/**
 * @brief Handler para templates processados
 * 
 * @param req Requisição HTTP
 * @param template_path Caminho do template
 * @param variables Array de variáveis
 * @param var_count Número de variáveis
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t spiffs_manager_template_handler(httpd_req_t *req,
                                         const char *template_path,
                                         const spiffs_template_var_t *variables,
                                         size_t var_count);

// ============================================================================
// REGISTRO AUTOMÁTICO DE HANDLERS
// ============================================================================

/**
 * @brief Registra todos os handlers básicos automaticamente
 * 
 * Registra handlers para:
 * - / (index)
 * - /css/*
 * - /js/*
 * - /html/* 
 * - /assets/*
 * 
 * @param server Handle do servidor HTTP
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t spiffs_manager_register_default_handlers(httpd_handle_t server);

/**
 * @brief Registra handler personalizado para um padrão de URI
 * 
 * @param server Handle do servidor HTTP
 * @param uri_pattern Padrão da URI (ex: "/api/files/*")
 * @param method Método HTTP
 * @param handler Handler customizado
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t spiffs_manager_register_custom_handler(httpd_handle_t server,
                                                 const char *uri_pattern,
                                                 httpd_method_t method,
                                                 httpd_handler_t handler);

// ============================================================================
// UTILITÁRIOS E HELPERS
// ============================================================================

/**
 * @brief Envia página de erro 404 personalizada
 * 
 * @param req Requisição HTTP
 * @param custom_message Mensagem customizada (opcional)
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t spiffs_manager_send_404(httpd_req_t *req, const char *custom_message);

/**
 * @brief Envia resposta de erro 500 personalizada
 * 
 * @param req Requisição HTTP
 * @param error_details Detalhes do erro (opcional)
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t spiffs_manager_send_500(httpd_req_t *req, const char *error_details);

/**
 * @brief Converte resultado da biblioteca para esp_err_t
 * 
 * @param result Resultado da biblioteca
 * @return esp_err_t Código de erro ESP-IDF equivalente
 */
esp_err_t spiffs_manager_result_to_esp_err(spiffs_manager_result_t result);

/**
 * @brief Obtém string descritiva do erro
 * 
 * @param result Código de resultado
 * @return const char* Descrição do erro
 */
const char* spiffs_manager_get_error_string(spiffs_manager_result_t result);

/**
 * @brief Registra preprocessador para determinado tipo de arquivo
 * 
 * @param extension Extensão do arquivo (ex: ".html")
 * @param preprocessor Função preprocessadora
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t spiffs_manager_register_preprocessor(const char *extension, spiffs_file_preprocessor_t preprocessor);

/**
 * @brief Limpa cache de arquivos (se habilitado)
 * 
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t spiffs_manager_clear_cache(void);

/**
 * @brief Obtém estatísticas de uso do manager
 * 
 * @param files_served Arquivos servidos (opcional)
 * @param cache_hits Cache hits (opcional) 
 * @param cache_misses Cache misses (opcional)
 * @param total_bytes_served Bytes totais servidos (opcional)
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t spiffs_manager_get_stats(uint32_t *files_served,
                                  uint32_t *cache_hits,
                                  uint32_t *cache_misses,
                                  size_t *total_bytes_served);

// ============================================================================
// MACROS DE CONVENIÊNCIA
// ============================================================================

/**
 * @brief Macro para criar array de variáveis de template
 * 
 * Uso:
 * SPIFFS_TEMPLATE_VARS(vars,
 *     "TITLE", "Minha Página",
 *     "USER", username,
 *     "TIME", current_time
 * );
 */
#define SPIFFS_TEMPLATE_VARS(name, ...) \
    spiffs_template_var_t name[] = { __VA_ARGS__ }; \
    size_t name##_count = sizeof(name) / sizeof(spiffs_template_var_t)

/**
 * @brief Macro para resposta rápida com template
 */
#define SPIFFS_SEND_TEMPLATE(req, template_path, vars) \
    spiffs_manager_template_handler(req, template_path, vars, sizeof(vars)/sizeof(spiffs_template_var_t))

/**
 * @brief Macro para resposta rápida com arquivo estático
 */
#define SPIFFS_SEND_FILE(req) \
    spiffs_manager_static_handler(req)

#ifdef __cplusplus
}
#endif

#endif // SPIFFS_FILE_MANAGER_H