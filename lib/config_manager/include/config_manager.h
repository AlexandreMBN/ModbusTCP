#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

/**
 * @file config_manager.h
 * @brief Biblioteca completa para gerenciamento de configurações JSON no ESP32
 * 
 * Esta biblioteca oferece:
 * - Gerenciamento unificado de configurações JSON
 * - Backup automático em NVS (memória não volátil)
 * - Validação de dados e recuperação de erro
 * - Sistema de usuários com níveis de acesso
 * - Compatibilidade com SPIFFS
 * - API extensível para configurações customizadas
 * 
 * @author ESP32 Development Team
 * @version 1.0.0
 * @date 2024
 */

#include <esp_err.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// CONSTANTES E CONFIGURAÇÕES
// ============================================================================

#define CONFIG_MANAGER_VERSION "1.0.0"

// Tamanhos máximos para strings
#define CONFIG_MANAGER_MAX_STR_LEN      128
#define CONFIG_MANAGER_MAX_PATH_LEN     256
#define CONFIG_MANAGER_MAX_JSON_SIZE    8192
#define CONFIG_MANAGER_MAX_CONFIGS      32

// Diretórios padrão
#define CONFIG_MANAGER_DEFAULT_BASE_PATH    "/spiffs"
#define CONFIG_MANAGER_DEFAULT_CONFIG_DIR   "/spiffs/data/config"
#define CONFIG_MANAGER_NVS_NAMESPACE        "config_backup"

// ============================================================================
// TIPOS E ENUMERAÇÕES
// ============================================================================

/**
 * @brief Códigos de resultado das operações
 */
typedef enum {
    CONFIG_MANAGER_OK = 0,                  ///< Sucesso
    CONFIG_MANAGER_ERROR_INVALID_ARG,       ///< Argumento inválido
    CONFIG_MANAGER_ERROR_FILE_NOT_FOUND,    ///< Arquivo não encontrado
    CONFIG_MANAGER_ERROR_JSON_PARSE,        ///< Erro ao parsear JSON
    CONFIG_MANAGER_ERROR_FILE_WRITE,        ///< Erro ao escrever arquivo
    CONFIG_MANAGER_ERROR_FILE_READ,         ///< Erro ao ler arquivo
    CONFIG_MANAGER_ERROR_NVS,               ///< Erro na NVS
    CONFIG_MANAGER_ERROR_OUT_OF_MEMORY,     ///< Sem memória
    CONFIG_MANAGER_ERROR_VALIDATION,        ///< Erro na validação
    CONFIG_MANAGER_ERROR_NOT_INITIALIZED,   ///< Biblioteca não inicializada
    CONFIG_MANAGER_ERROR_ACCESS_DENIED,     ///< Acesso negado
    CONFIG_MANAGER_ERROR_CONFIG_EXISTS,     ///< Configuração já existe
    CONFIG_MANAGER_ERROR_SPIFFS_NOT_MOUNTED ///< SPIFFS não montado
} config_manager_result_t;

/**
 * @brief Níveis de usuário para controle de acesso
 */
typedef enum {
    CONFIG_USER_LEVEL_NONE = 0,     ///< Não logado
    CONFIG_USER_LEVEL_BASIC = 1,    ///< Usuário básico
    CONFIG_USER_LEVEL_ADMIN = 2,    ///< Administrador
    CONFIG_USER_LEVEL_ROOT = 3      ///< Super administrador
} config_user_level_t;

/**
 * @brief Tipos de dados suportados
 */
typedef enum {
    CONFIG_TYPE_STRING,
    CONFIG_TYPE_INTEGER,
    CONFIG_TYPE_DOUBLE,
    CONFIG_TYPE_BOOLEAN,
    CONFIG_TYPE_ARRAY,
    CONFIG_TYPE_OBJECT
} config_data_type_t;

/**
 * @brief Configuração da biblioteca
 */
typedef struct {
    const char *base_path;              ///< Caminho base do SPIFFS
    const char *config_dir;             ///< Diretório de configurações
    const char *nvs_namespace;          ///< Namespace da NVS
    bool enable_nvs_backup;             ///< Habilitar backup na NVS
    bool enable_validation;             ///< Habilitar validação
    bool enable_legacy_paths;           ///< Suporte a caminhos antigos
    bool auto_create_dirs;              ///< Criar diretórios automaticamente
    size_t max_file_size;               ///< Tamanho máximo de arquivo
    config_user_level_t min_level_read; ///< Nível mínimo para leitura
    config_user_level_t min_level_write;///< Nível mínimo para escrita
} config_manager_config_t;

/**
 * @brief Estrutura para validação de campos
 */
typedef struct {
    const char *field_name;             ///< Nome do campo
    config_data_type_t type;            ///< Tipo esperado
    bool required;                      ///< Campo obrigatório
    const void *min_value;              ///< Valor mínimo (para números)
    const void *max_value;              ///< Valor máximo (para números)
    const char *regex_pattern;          ///< Padrão regex (para strings)
    const char **allowed_values;        ///< Valores permitidos
} config_field_validator_t;

/**
 * @brief Schema de validação para uma configuração
 */
typedef struct {
    const char *config_name;            ///< Nome da configuração
    const config_field_validator_t *fields; ///< Array de campos
    size_t field_count;                 ///< Número de campos
    config_user_level_t required_level; ///< Nível necessário para modificar
} config_schema_t;

/**
 * @brief Callback para processamento de configurações
 */
typedef config_manager_result_t (*config_processor_t)(const char *config_name, 
                                                      cJSON *json_data, 
                                                      void *user_data);

/**
 * @brief Callback para notificação de mudanças
 */
typedef void (*config_change_callback_t)(const char *config_name, 
                                         const cJSON *old_data, 
                                         const cJSON *new_data, 
                                         void *user_data);

// ============================================================================
// CONFIGURAÇÕES PRÉ-DEFINIDAS (COMPATIBILIDADE)
// ============================================================================

/**
 * @brief Configuração WiFi Access Point
 */
typedef struct {
    char ssid[32];          ///< Nome da rede
    char password[64];      ///< Senha da rede
    char ip[16];            ///< IP do AP
    char username[32];      ///< Usuário admin
    uint8_t channel;        ///< Canal WiFi
    uint8_t max_connections;///< Máximo de conexões
    bool hidden;            ///< Rede oculta
} config_ap_t;

/**
 * @brief Configuração WiFi Station
 */
typedef struct {
    char ssid[32];          ///< Nome da rede
    char password[64];      ///< Senha da rede
    bool enable_static_ip;  ///< IP estático
    char static_ip[16];     ///< IP estático
    char subnet[16];        ///< Máscara de rede
    char gateway[16];       ///< Gateway
    char dns[16];           ///< DNS
} config_sta_t;

/**
 * @brief Configuração MQTT
 */
typedef struct {
    char broker_url[128];   ///< URL do broker
    char client_id[32];     ///< ID do cliente
    char username[32];      ///< Usuário MQTT
    char password[64];      ///< Senha MQTT
    uint16_t port;          ///< Porta do broker
    uint8_t qos;            ///< Quality of Service
    bool retain;            ///< Retain messages
    bool tls_enabled;       ///< TLS habilitado
    char ca_path[128];      ///< Caminho do certificado CA
    bool enabled;           ///< MQTT habilitado
    uint32_t publish_interval_ms; ///< Intervalo de publicação
    uint32_t keep_alive;    ///< Keep alive
    bool clean_session;     ///< Sessão limpa
} config_mqtt_t;

/**
 * @brief Configuração de rede
 */
typedef struct {
    char ip[16];            ///< Endereço IP
    char mask[16];          ///< Máscara de rede
    char gateway[16];       ///< Gateway
    char dns[16];           ///< DNS
} config_network_t;

// ============================================================================
// API PRINCIPAL
// ============================================================================

/**
 * @brief Obter configuração padrão da biblioteca
 */
void config_manager_get_default_config(config_manager_config_t *config);

/**
 * @brief Inicializar a biblioteca de gerenciamento de configurações
 * 
 * @param config Configuração da biblioteca (NULL para usar padrão)
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_init(const config_manager_config_t *config);

/**
 * @brief Desinicializar a biblioteca
 * 
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_deinit(void);

/**
 * @brief Verificar se a biblioteca está inicializada
 * 
 * @return bool true se inicializada
 */
bool config_manager_is_initialized(void);

// ============================================================================
// OPERAÇÕES COM CONFIGURAÇÕES JSON
// ============================================================================

/**
 * @brief Salvar configuração JSON no arquivo
 * 
 * @param config_name Nome da configuração
 * @param json_data Dados JSON
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_save_json(const char *config_name, const cJSON *json_data);

/**
 * @brief Carregar configuração JSON do arquivo
 * 
 * @param config_name Nome da configuração
 * @param json_data Ponteiro para receber os dados JSON (deve ser liberado)
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_load_json(const char *config_name, cJSON **json_data);

/**
 * @brief Salvar string JSON diretamente
 * 
 * @param config_name Nome da configuração
 * @param json_string String JSON
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_save_json_string(const char *config_name, const char *json_string);

/**
 * @brief Carregar string JSON
 * 
 * @param config_name Nome da configuração
 * @param json_string Ponteiro para receber a string (deve ser liberado)
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_load_json_string(const char *config_name, char **json_string);

/**
 * @brief Verificar se configuração existe
 * 
 * @param config_name Nome da configuração
 * @return bool true se existe
 */
bool config_manager_exists(const char *config_name);

/**
 * @brief Excluir configuração
 * 
 * @param config_name Nome da configuração
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_delete(const char *config_name);

/**
 * @brief Listar todas as configurações
 * 
 * @param config_list Array para receber os nomes
 * @param max_configs Tamanho máximo do array
 * @param count Ponteiro para receber o número de configurações
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_list_configs(char config_list[][CONFIG_MANAGER_MAX_STR_LEN], 
                                                   size_t max_configs, 
                                                   size_t *count);

// ============================================================================
// SISTEMA DE VALIDAÇÃO
// ============================================================================

/**
 * @brief Registrar schema de validação
 * 
 * @param schema Schema de validação
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_register_schema(const config_schema_t *schema);

/**
 * @brief Validar configuração JSON
 * 
 * @param config_name Nome da configuração
 * @param json_data Dados JSON para validar
 * @return config_manager_result_t Resultado da validação
 */
config_manager_result_t config_manager_validate_json(const char *config_name, const cJSON *json_data);

/**
 * @brief Obter schema registrado
 * 
 * @param config_name Nome da configuração
 * @return const config_schema_t* Schema ou NULL se não encontrado
 */
const config_schema_t* config_manager_get_schema(const char *config_name);

// ============================================================================
// BACKUP E RECUPERAÇÃO (NVS)
// ============================================================================

/**
 * @brief Fazer backup de configuração na NVS
 * 
 * @param config_name Nome da configuração
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_backup_to_nvs(const char *config_name);

/**
 * @brief Restaurar configuração da NVS
 * 
 * @param config_name Nome da configuração
 * @param overwrite_existing Sobrescrever se já existe
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_restore_from_nvs(const char *config_name, bool overwrite_existing);

/**
 * @brief Fazer backup de todas as configurações
 * 
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_backup_all_to_nvs(void);

/**
 * @brief Restaurar todas as configurações da NVS
 * 
 * @param overwrite_existing Sobrescrever arquivos existentes
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_restore_all_from_nvs(bool overwrite_existing);

/**
 * @brief Limpar backup da NVS
 * 
 * @param config_name Nome da configuração (NULL para todas)
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_clear_nvs_backup(const char *config_name);

// ============================================================================
// SISTEMA DE USUÁRIOS E CONTROLE DE ACESSO
// ============================================================================

/**
 * @brief Definir nível do usuário atual
 * 
 * @param level Nível de usuário
 */
void config_manager_set_user_level(config_user_level_t level);

/**
 * @brief Obter nível do usuário atual
 * 
 * @return config_user_level_t Nível atual
 */
config_user_level_t config_manager_get_user_level(void);

/**
 * @brief Verificar permissão de acesso
 * 
 * @param config_name Nome da configuração
 * @param write_access true para escrita, false para leitura
 * @return bool true se tem permissão
 */
bool config_manager_check_access(const char *config_name, bool write_access);

/**
 * @brief Salvar estado de login
 * 
 * @param logged_in Estado do login
 * @param level Nível de usuário
 */
void config_manager_save_login_state(bool logged_in, config_user_level_t level);

/**
 * @brief Carregar estado de login
 * 
 * @param logged_in Ponteiro para receber estado
 * @param level Ponteiro para receber nível
 */
void config_manager_load_login_state(bool *logged_in, config_user_level_t *level);

// ============================================================================
// PROCESSAMENTO E CALLBACKS
// ============================================================================

/**
 * @brief Registrar processador para uma configuração
 * 
 * @param config_name Nome da configuração
 * @param processor Função de processamento
 * @param user_data Dados do usuário
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_register_processor(const char *config_name, 
                                                          config_processor_t processor, 
                                                          void *user_data);

/**
 * @brief Registrar callback para mudanças de configuração
 * 
 * @param config_name Nome da configuração (NULL para todas)
 * @param callback Função de callback
 * @param user_data Dados do usuário
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_register_change_callback(const char *config_name, 
                                                               config_change_callback_t callback, 
                                                               void *user_data);

// ============================================================================
// CONFIGURAÇÕES PRÉ-DEFINIDAS (COMPATIBILIDADE)
// ============================================================================

/**
 * @brief Salvar configuração AP
 * 
 * @param config Configuração AP
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_save_ap_config(const config_ap_t *config);

/**
 * @brief Carregar configuração AP
 * 
 * @param config Estrutura para receber a configuração
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_load_ap_config(config_ap_t *config);

/**
 * @brief Salvar configuração STA
 * 
 * @param config Configuração STA
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_save_sta_config(const config_sta_t *config);

/**
 * @brief Carregar configuração STA
 * 
 * @param config Estrutura para receber a configuração
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_load_sta_config(config_sta_t *config);

/**
 * @brief Salvar configuração MQTT
 * 
 * @param config Configuração MQTT
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_save_mqtt_config(const config_mqtt_t *config);

/**
 * @brief Carregar configuração MQTT
 * 
 * @param config Estrutura para receber a configuração
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_load_mqtt_config(config_mqtt_t *config);

/**
 * @brief Salvar configuração de rede
 * 
 * @param config Configuração de rede
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_save_network_config(const config_network_t *config);

/**
 * @brief Carregar configuração de rede
 * 
 * @param config Estrutura para receber a configuração
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_load_network_config(config_network_t *config);

// ============================================================================
// UTILITÁRIOS
// ============================================================================

/**
 * @brief Converter resultado para string
 * 
 * @param result Código de resultado
 * @return const char* Descrição do erro
 */
const char* config_manager_get_error_string(config_manager_result_t result);

/**
 * @brief Converter resultado para esp_err_t
 * 
 * @param result Resultado da biblioteca
 * @return esp_err_t Código ESP-IDF equivalente
 */
esp_err_t config_manager_result_to_esp_err(config_manager_result_t result);

/**
 * @brief Obter informações sobre o sistema de configurações
 * 
 * @param total_configs Número total de configurações
 * @param nvs_backups Número de backups na NVS
 * @param memory_used Memória usada aproximada
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_get_system_info(size_t *total_configs, 
                                                       size_t *nvs_backups, 
                                                       size_t *memory_used);

/**
 * @brief Criar diretórios de configuração se não existirem
 * 
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_ensure_config_dirs(void);

/**
 * @brief Remontar SPIFFS (limpa cache interno)
 * 
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_remount_spiffs(void);

/**
 * @brief Executar diagnóstico do sistema de configurações
 * 
 * @param report Buffer para receber relatório
 * @param report_size Tamanho do buffer
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_run_diagnostics(char *report, size_t report_size);

// ============================================================================
// MIGRAÇÃO E COMPATIBILIDADE
// ============================================================================

/**
 * @brief Migrar configurações do formato antigo para o novo
 * 
 * @param old_path Caminho do arquivo antigo
 * @param new_config_name Nome da nova configuração
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_migrate_config(const char *old_path, const char *new_config_name);

/**
 * @brief Verificar se há configurações para migrar
 * 
 * @param configs_to_migrate Array para receber nomes de configs
 * @param max_configs Tamanho máximo do array
 * @param count Número de configurações encontradas
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_check_migration_needed(char configs_to_migrate[][CONFIG_MANAGER_MAX_STR_LEN], 
                                                             size_t max_configs, 
                                                             size_t *count);

/**
 * @brief Migrar todas as configurações automaticamente
 * 
 * @return config_manager_result_t Resultado da operação
 */
config_manager_result_t config_manager_auto_migrate_all(void);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_MANAGER_H