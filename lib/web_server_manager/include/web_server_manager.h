#ifndef WEB_SERVER_MANAGER_H
#define WEB_SERVER_MANAGER_H

#include <esp_http_server.h>
#include <esp_err.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// CONSTANTES E CONFIGURAÇÕES
// =============================================================================

#define WEB_SERVER_MAX_URI_HANDLERS     64
#define WEB_SERVER_MAX_URI_LEN          256
#define WEB_SERVER_MAX_TEMPLATE_SIZE    8192
#define WEB_SERVER_MAX_SUBSTITUTIONS    32
#define WEB_SERVER_MAX_MIDDLEWARE       8

// =============================================================================
// ENUMERAÇÕES
// =============================================================================

/**
 * @brief Níveis de autenticação do usuário
 */
typedef enum {
    WSM_USER_LEVEL_NONE = 0,    /**< Nenhuma autenticação */
    WSM_USER_LEVEL_BASIC,       /**< Usuário básico */
    WSM_USER_LEVEL_ADMIN        /**< Administrador */
} wsm_user_level_t;

/**
 * @brief Status do servidor web
 */
typedef enum {
    WSM_STATUS_STOPPED = 0,     /**< Servidor parado */
    WSM_STATUS_STARTING,        /**< Iniciando */
    WSM_STATUS_RUNNING,         /**< Executando */
    WSM_STATUS_STOPPING,        /**< Parando */
    WSM_STATUS_ERROR           /**< Erro */
} wsm_status_t;

/**
 * @brief Tipos de middleware
 */
typedef enum {
    WSM_MIDDLEWARE_AUTH = 0,    /**< Middleware de autenticação */
    WSM_MIDDLEWARE_CORS,        /**< Middleware de CORS */
    WSM_MIDDLEWARE_LOGGING,     /**< Middleware de logging */
    WSM_MIDDLEWARE_RATE_LIMIT,  /**< Middleware de rate limiting */
    WSM_MIDDLEWARE_CUSTOM       /**< Middleware customizado */
} wsm_middleware_type_t;

// =============================================================================
// ESTRUTURAS
// =============================================================================

/**
 * @brief Configuração do servidor web
 */
typedef struct {
    uint16_t port;                          /**< Porta do servidor */
    uint16_t max_open_sockets;              /**< Máximo de sockets abertos */
    uint16_t max_uri_handlers;              /**< Máximo de handlers URI */
    uint16_t max_resp_headers;              /**< Máximo de headers de resposta */
    size_t stack_size;                      /**< Tamanho da pilha da tarefa */
    uint8_t task_priority;                  /**< Prioridade da tarefa */
    uint32_t recv_wait_timeout;             /**< Timeout de recepção */
    uint32_t send_wait_timeout;             /**< Timeout de envio */
    bool enable_cors;                       /**< Habilitar CORS */
    bool enable_logging;                    /**< Habilitar logging */
    char spiffs_mount_point[32];            /**< Ponto de montagem SPIFFS */
    char static_files_path[64];             /**< Caminho dos arquivos estáticos */
    char templates_path[64];                /**< Caminho dos templates */
} wsm_config_t;

/**
 * @brief Contexto de template
 */
typedef struct {
    char key[64];                           /**< Chave do placeholder */
    char value[256];                        /**< Valor para substituição */
} wsm_template_substitution_t;

/**
 * @brief Contexto de resposta de template
 */
typedef struct {
    wsm_template_substitution_t substitutions[WEB_SERVER_MAX_SUBSTITUTIONS];
    size_t count;                           /**< Número de substituições */
} wsm_template_context_t;

/**
 * @brief Handler de middleware
 * @param req Requisição HTTP
 * @param user_data Dados do usuário
 * @return ESP_OK para continuar, ESP_FAIL para interromper
 */
typedef esp_err_t (*wsm_middleware_handler_t)(httpd_req_t *req, void *user_data);

/**
 * @brief Configuração de middleware
 */
typedef struct {
    wsm_middleware_type_t type;             /**< Tipo do middleware */
    wsm_middleware_handler_t handler;       /**< Função do middleware */
    void *user_data;                        /**< Dados do usuário */
    bool enabled;                           /**< Middleware habilitado */
} wsm_middleware_t;

/**
 * @brief Handler de rota personalizada
 */
typedef esp_err_t (*wsm_route_handler_t)(httpd_req_t *req);

/**
 * @brief Configuração de rota
 */
typedef struct {
    char uri[WEB_SERVER_MAX_URI_LEN];       /**< URI da rota */
    httpd_method_t method;                  /**< Método HTTP */
    wsm_route_handler_t handler;            /**< Handler da rota */
    wsm_user_level_t auth_level;            /**< Nível de autenticação necessário */
    void *user_data;                        /**< Dados do usuário */
    bool enable_middleware;                 /**< Aplicar middleware */
} wsm_route_config_t;

/**
 * @brief Estatísticas do servidor
 */
typedef struct {
    uint32_t requests_total;                /**< Total de requisições */
    uint32_t requests_success;              /**< Requisições bem-sucedidas */
    uint32_t requests_error;                /**< Requisições com erro */
    uint32_t bytes_sent;                    /**< Bytes enviados */
    uint32_t bytes_received;                /**< Bytes recebidos */
    uint32_t active_connections;            /**< Conexões ativas */
    uint32_t uptime_seconds;                /**< Tempo de atividade em segundos */
} wsm_stats_t;

// =============================================================================
// FUNÇÕES DE INICIALIZAÇÃO E CONFIGURAÇÃO
// =============================================================================

/**
 * @brief Obter configuração padrão do servidor web
 * @param config Ponteiro para estrutura de configuração
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_get_default_config(wsm_config_t *config);

/**
 * @brief Inicializar o Web Server Manager
 * @param config Configuração do servidor (NULL para usar padrão)
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_init(const wsm_config_t *config);

/**
 * @brief Iniciar o servidor web
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_start(void);

/**
 * @brief Parar o servidor web
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_stop(void);

/**
 * @brief Desinicializar o Web Server Manager
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_deinit(void);

/**
 * @brief Obter status atual do servidor
 * @return Status atual
 */
wsm_status_t wsm_get_status(void);

/**
 * @brief Obter estatísticas do servidor
 * @param stats Ponteiro para estrutura de estatísticas
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_get_stats(wsm_stats_t *stats);

// =============================================================================
// FUNÇÕES DE ROTEAMENTO
// =============================================================================

/**
 * @brief Registrar uma nova rota
 * @param route_config Configuração da rota
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_register_route(const wsm_route_config_t *route_config);

/**
 * @brief Remover uma rota
 * @param uri URI da rota a ser removida
 * @param method Método HTTP
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_unregister_route(const char *uri, httpd_method_t method);

/**
 * @brief Registrar múltiplas rotas de uma vez
 * @param routes Array de configurações de rota
 * @param count Número de rotas no array
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_register_routes(const wsm_route_config_t *routes, size_t count);

// =============================================================================
// FUNÇÕES DE MIDDLEWARE
// =============================================================================

/**
 * @brief Registrar middleware
 * @param middleware Configuração do middleware
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_register_middleware(const wsm_middleware_t *middleware);

/**
 * @brief Habilitar/desabilitar middleware
 * @param type Tipo do middleware
 * @param enabled Estado desejado
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_set_middleware_enabled(wsm_middleware_type_t type, bool enabled);

// =============================================================================
// FUNÇÕES DE AUTENTICAÇÃO
// =============================================================================

/**
 * @brief Verificar permissão de acesso
 * @param req Requisição HTTP
 * @param required_level Nível necessário
 * @return ESP_OK se autorizado, ESP_ERR_NOT_ALLOWED caso contrário
 */
esp_err_t wsm_check_auth(httpd_req_t *req, wsm_user_level_t required_level);

/**
 * @brief Definir nível de usuário para sessão
 * @param req Requisição HTTP
 * @param level Nível do usuário
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_set_user_level(httpd_req_t *req, wsm_user_level_t level);

/**
 * @brief Obter nível atual do usuário
 * @param req Requisição HTTP
 * @return Nível do usuário
 */
wsm_user_level_t wsm_get_user_level(httpd_req_t *req);

/**
 * @brief Configurar credenciais de login
 * @param basic_user Usuário básico
 * @param basic_pass Senha do usuário básico
 * @param admin_user Usuário administrador
 * @param admin_pass Senha do administrador
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_set_auth_credentials(const char *basic_user, const char *basic_pass,
                                   const char *admin_user, const char *admin_pass);

// =============================================================================
// FUNÇÕES DE TEMPLATE
// =============================================================================

/**
 * @brief Inicializar contexto de template
 * @param context Contexto a ser inicializado
 */
void wsm_template_context_init(wsm_template_context_t *context);

/**
 * @brief Adicionar substituição ao contexto
 * @param context Contexto do template
 * @param key Chave do placeholder
 * @param value Valor para substituição
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_template_add_substitution(wsm_template_context_t *context,
                                        const char *key, const char *value);

/**
 * @brief Renderizar template com substituições
 * @param template_path Caminho do template
 * @param context Contexto com substituições
 * @param output Buffer de saída (será alocado)
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_render_template(const char *template_path,
                              const wsm_template_context_t *context,
                              char **output);

/**
 * @brief Responder com template renderizado
 * @param req Requisição HTTP
 * @param template_path Caminho do template
 * @param context Contexto com substituições
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_respond_with_template(httpd_req_t *req, const char *template_path,
                                    const wsm_template_context_t *context);

// =============================================================================
// FUNÇÕES DE ARQUIVOS ESTÁTICOS
// =============================================================================

/**
 * @brief Servir arquivo estático
 * @param req Requisição HTTP
 * @param file_path Caminho do arquivo
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_serve_static_file(httpd_req_t *req, const char *file_path);

/**
 * @brief Registrar diretório de arquivos estáticos
 * @param uri_prefix Prefixo URI (ex: "/static")
 * @param file_path_prefix Prefixo do caminho no sistema de arquivos
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_register_static_handler(const char *uri_prefix, const char *file_path_prefix);

/**
 * @brief Obter tipo MIME baseado na extensão do arquivo
 * @param file_path Caminho do arquivo
 * @return String com tipo MIME
 */
const char *wsm_get_mime_type(const char *file_path);

// =============================================================================
// FUNÇÕES UTILITÁRIAS
// =============================================================================

/**
 * @brief Enviar resposta JSON
 * @param req Requisição HTTP
 * @param json_string String JSON
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_send_json_response(httpd_req_t *req, const char *json_string);

/**
 * @brief Enviar resposta de erro
 * @param req Requisição HTTP
 * @param code Código de erro HTTP
 * @param message Mensagem de erro
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_send_error_response(httpd_req_t *req, int code, const char *message);

/**
 * @brief Enviar página de confirmação
 * @param req Requisição HTTP
 * @param title Título da página
 * @param message Mensagem de confirmação
 * @param redirect_url URL de redirecionamento
 * @param redirect_delay Delay em segundos (0 para não redirecionar)
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_send_confirmation_page(httpd_req_t *req, const char *title,
                                     const char *message, const char *redirect_url,
                                     int redirect_delay);

/**
 * @brief Extrair valor de formulário
 * @param req Requisição HTTP
 * @param key Chave do campo
 * @param value Buffer para armazenar valor
 * @param value_size Tamanho do buffer
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_extract_form_value(httpd_req_t *req, const char *key,
                                 char *value, size_t value_size);

/**
 * @brief URL decode in-place
 * @param str String a ser decodificada
 */
void wsm_url_decode_inplace(char *str);

/**
 * @brief HTML escape
 * @param input String de entrada
 * @param output Buffer de saída
 * @param output_size Tamanho do buffer de saída
 */
void wsm_html_escape(const char *input, char *output, size_t output_size);

// =============================================================================
// HANDLERS INTEGRADOS PRONTOS PARA USO
// =============================================================================

/**
 * @brief Handler para página de login
 */
esp_err_t wsm_login_handler(httpd_req_t *req);

/**
 * @brief Handler para processar login
 */
esp_err_t wsm_login_post_handler(httpd_req_t *req);

/**
 * @brief Handler para logout
 */
esp_err_t wsm_logout_handler(httpd_req_t *req);

/**
 * @brief Handler para página de informações do sistema
 */
esp_err_t wsm_system_info_handler(httpd_req_t *req);

/**
 * @brief Handler para reiniciar o sistema
 */
esp_err_t wsm_system_restart_handler(httpd_req_t *req);

/**
 * @brief Handler para factory reset
 */
esp_err_t wsm_factory_reset_handler(httpd_req_t *req);

// =============================================================================
// INTEGRAÇÃO COM OUTRAS BIBLIOTECAS
// =============================================================================

/**
 * @brief Registrar handlers de integração com WiFi Manager
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_register_wifi_handlers(void);

/**
 * @brief Registrar handlers de integração com Config Manager
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_register_config_handlers(void);

/**
 * @brief Registrar handlers de integração com MQTT Client Manager
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wsm_register_mqtt_handlers(void);

#ifdef __cplusplus
}
#endif

#endif /* WEB_SERVER_MANAGER_H */