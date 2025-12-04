#include "config_manager.h"
#include <esp_spiffs.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <dirent.h>

static const char *TAG = "CONFIG_MGR";

// ============================================================================
// ESTADO INTERNO DA BIBLIOTECA
// ============================================================================

// Estado de inicialização
static bool g_initialized = false;

// Configuração atual
static config_manager_config_t g_config = {0};

// Estado do usuário atual
static config_user_level_t g_current_user_level = CONFIG_USER_LEVEL_NONE;

// Schemas registrados
#define MAX_SCHEMAS 32
static config_schema_t g_schemas[MAX_SCHEMAS];
static size_t g_schema_count = 0;

// Processadores registrados
#define MAX_PROCESSORS 16
static struct {
    char config_name[CONFIG_MANAGER_MAX_STR_LEN];
    config_processor_t processor;
    void *user_data;
} g_processors[MAX_PROCESSORS];
static size_t g_processor_count = 0;

// Callbacks de mudança
#define MAX_CALLBACKS 8
static struct {
    char config_name[CONFIG_MANAGER_MAX_STR_LEN]; // "" = todas
    config_change_callback_t callback;
    void *user_data;
} g_callbacks[MAX_CALLBACKS];
static size_t g_callback_count = 0;

// ============================================================================
// FUNÇÕES INTERNAS
// ============================================================================

static esp_err_t ensure_spiffs_mounted(void) {
    static bool mounted = false;
    if (mounted) return ESP_OK;
    
    ESP_LOGI(TAG, "Montando SPIFFS...");
    
    esp_vfs_spiffs_conf_t conf = {
        .base_path = g_config.base_path,
        .partition_label = NULL,
        .max_files = 10,
        .format_if_mount_failed = true,
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Verificar espaço disponível
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS: %d KB total, %d KB used", total / 1024, used / 1024);
    }
    
    mounted = true;
    return ESP_OK;
}

static config_manager_result_t ensure_directories(void) {
    if (!g_config.auto_create_dirs) {
        return CONFIG_MANAGER_OK;
    }
    
    // Criar diretório base se não existir
    struct stat st = {0};
    if (stat(g_config.base_path, &st) == -1) {
        if (mkdir(g_config.base_path, 0700) != 0) {
            ESP_LOGE(TAG, "Failed to create base directory: %s", g_config.base_path);
            return CONFIG_MANAGER_ERROR_FILE_WRITE;
        }
    }
    
    // Criar diretório de configurações
    if (stat(g_config.config_dir, &st) == -1) {
        // Criar /spiffs/data primeiro se necessário
        char data_dir[CONFIG_MANAGER_MAX_PATH_LEN];
        snprintf(data_dir, sizeof(data_dir), "%s/data", g_config.base_path);
        if (stat(data_dir, &st) == -1) {
            if (mkdir(data_dir, 0700) != 0) {
                ESP_LOGE(TAG, "Failed to create data directory: %s", data_dir);
                return CONFIG_MANAGER_ERROR_FILE_WRITE;
            }
        }
        
        // Criar diretório de configurações
        if (mkdir(g_config.config_dir, 0700) != 0) {
            ESP_LOGE(TAG, "Failed to create config directory: %s", g_config.config_dir);
            return CONFIG_MANAGER_ERROR_FILE_WRITE;
        }
    }
    
    ESP_LOGD(TAG, "Directories ensured: %s", g_config.config_dir);
    return CONFIG_MANAGER_OK;
}

static void build_config_path(const char *config_name, char *path, size_t path_size) {
    snprintf(path, path_size, "%s/%s.json", g_config.config_dir, config_name);
}

static void build_legacy_path(const char *config_name, char *path, size_t path_size) {
    snprintf(path, path_size, "%s/%s.json", g_config.base_path, config_name);
}

static config_manager_result_t save_json_to_nvs(const char *config_name, const char *json_string) {
    if (!g_config.enable_nvs_backup) {
        return CONFIG_MANAGER_OK; // Não é erro se backup está desabilitado
    }
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(g_config.nvs_namespace, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao abrir NVS namespace %s: %s", g_config.nvs_namespace, esp_err_to_name(err));
        return CONFIG_MANAGER_ERROR_NVS;
    }
    
    err = nvs_set_str(nvs_handle, config_name, json_string);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao salvar %s na NVS: %s", config_name, esp_err_to_name(err));
        nvs_close(nvs_handle);
        return CONFIG_MANAGER_ERROR_NVS;
    }
    
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    if (err == ESP_OK) {
        ESP_LOGD(TAG, "Backup %s salvo na NVS", config_name);
        return CONFIG_MANAGER_OK;
    } else {
        ESP_LOGE(TAG, "Erro ao commitar %s na NVS: %s", config_name, esp_err_to_name(err));
        return CONFIG_MANAGER_ERROR_NVS;
    }
}

static config_manager_result_t load_json_from_nvs(const char *config_name, char **json_string) {
    if (!g_config.enable_nvs_backup) {
        return CONFIG_MANAGER_ERROR_FILE_NOT_FOUND;
    }
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(g_config.nvs_namespace, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "NVS namespace %s não encontrado: %s", g_config.nvs_namespace, esp_err_to_name(err));
        return CONFIG_MANAGER_ERROR_NVS;
    }
    
    size_t required_size = 0;
    err = nvs_get_str(nvs_handle, config_name, NULL, &required_size);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Backup %s não encontrado na NVS: %s", config_name, esp_err_to_name(err));
        nvs_close(nvs_handle);
        return CONFIG_MANAGER_ERROR_FILE_NOT_FOUND;
    }
    
    *json_string = malloc(required_size);
    if (*json_string == NULL) {
        ESP_LOGE(TAG, "Erro de alocação de memória para %s", config_name);
        nvs_close(nvs_handle);
        return CONFIG_MANAGER_ERROR_OUT_OF_MEMORY;
    }
    
    err = nvs_get_str(nvs_handle, config_name, *json_string, &required_size);
    nvs_close(nvs_handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao carregar %s da NVS: %s", config_name, esp_err_to_name(err));
        free(*json_string);
        *json_string = NULL;
        return CONFIG_MANAGER_ERROR_NVS;
    }
    
    ESP_LOGD(TAG, "Backup %s carregado da NVS", config_name);
    return CONFIG_MANAGER_OK;
}

static const config_schema_t* find_schema(const char *config_name) {
    for (size_t i = 0; i < g_schema_count; i++) {
        if (strcmp(g_schemas[i].config_name, config_name) == 0) {
            return &g_schemas[i];
        }
    }
    return NULL;
}

static config_processor_t find_processor(const char *config_name, void **user_data) {
    for (size_t i = 0; i < g_processor_count; i++) {
        if (strcmp(g_processors[i].config_name, config_name) == 0) {
            if (user_data) *user_data = g_processors[i].user_data;
            return g_processors[i].processor;
        }
    }
    return NULL;
}

static void notify_config_change(const char *config_name, const cJSON *old_data, const cJSON *new_data) {
    for (size_t i = 0; i < g_callback_count; i++) {
        // Callback específico para esta config ou callback global
        if (strlen(g_callbacks[i].config_name) == 0 || 
            strcmp(g_callbacks[i].config_name, config_name) == 0) {
            g_callbacks[i].callback(config_name, old_data, new_data, g_callbacks[i].user_data);
        }
    }
}

static config_manager_result_t validate_field(const config_field_validator_t *field, const cJSON *value) {
    if (!field || !value) {
        return CONFIG_MANAGER_ERROR_INVALID_ARG;
    }
    
    // Verificar tipo
    switch (field->type) {
        case CONFIG_TYPE_STRING:
            if (!cJSON_IsString(value)) {
                ESP_LOGE(TAG, "Campo %s deve ser string", field->field_name);
                return CONFIG_MANAGER_ERROR_VALIDATION;
            }
            break;
            
        case CONFIG_TYPE_INTEGER:
            if (!cJSON_IsNumber(value)) {
                ESP_LOGE(TAG, "Campo %s deve ser número inteiro", field->field_name);
                return CONFIG_MANAGER_ERROR_VALIDATION;
            }
            break;
            
        case CONFIG_TYPE_DOUBLE:
            if (!cJSON_IsNumber(value)) {
                ESP_LOGE(TAG, "Campo %s deve ser número", field->field_name);
                return CONFIG_MANAGER_ERROR_VALIDATION;
            }
            break;
            
        case CONFIG_TYPE_BOOLEAN:
            if (!cJSON_IsBool(value)) {
                ESP_LOGE(TAG, "Campo %s deve ser boolean", field->field_name);
                return CONFIG_MANAGER_ERROR_VALIDATION;
            }
            break;
            
        case CONFIG_TYPE_ARRAY:
            if (!cJSON_IsArray(value)) {
                ESP_LOGE(TAG, "Campo %s deve ser array", field->field_name);
                return CONFIG_MANAGER_ERROR_VALIDATION;
            }
            break;
            
        case CONFIG_TYPE_OBJECT:
            if (!cJSON_IsObject(value)) {
                ESP_LOGE(TAG, "Campo %s deve ser objeto", field->field_name);
                return CONFIG_MANAGER_ERROR_VALIDATION;
            }
            break;
    }
    
    // Validações específicas para strings
    if (field->type == CONFIG_TYPE_STRING && cJSON_IsString(value)) {
        const char *str_value = cJSON_GetStringValue(value);
        
        // Verificar valores permitidos
        if (field->allowed_values) {
            bool found = false;
            for (int i = 0; field->allowed_values[i] != NULL; i++) {
                if (strcmp(str_value, field->allowed_values[i]) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                ESP_LOGE(TAG, "Campo %s tem valor inválido: %s", field->field_name, str_value);
                return CONFIG_MANAGER_ERROR_VALIDATION;
            }
        }
    }
    
    // Validações para números
    if ((field->type == CONFIG_TYPE_INTEGER || field->type == CONFIG_TYPE_DOUBLE) && cJSON_IsNumber(value)) {
        double num_value = cJSON_GetNumberValue(value);
        
        if (field->min_value) {
            double min_val = *(double*)field->min_value;
            if (num_value < min_val) {
                ESP_LOGE(TAG, "Campo %s menor que mínimo: %.2f < %.2f", field->field_name, num_value, min_val);
                return CONFIG_MANAGER_ERROR_VALIDATION;
            }
        }
        
        if (field->max_value) {
            double max_val = *(double*)field->max_value;
            if (num_value > max_val) {
                ESP_LOGE(TAG, "Campo %s maior que máximo: %.2f > %.2f", field->field_name, num_value, max_val);
                return CONFIG_MANAGER_ERROR_VALIDATION;
            }
        }
    }
    
    return CONFIG_MANAGER_OK;
}

// ============================================================================
// API PÚBLICA
// ============================================================================

void config_manager_get_default_config(config_manager_config_t *config) {
    if (!config) return;
    
    config->base_path = CONFIG_MANAGER_DEFAULT_BASE_PATH;
    config->config_dir = CONFIG_MANAGER_DEFAULT_CONFIG_DIR;
    config->nvs_namespace = CONFIG_MANAGER_NVS_NAMESPACE;
    config->enable_nvs_backup = true;
    config->enable_validation = true;
    config->enable_legacy_paths = true;
    config->auto_create_dirs = true;
    config->max_file_size = CONFIG_MANAGER_MAX_JSON_SIZE;
    config->min_level_read = CONFIG_USER_LEVEL_NONE;
    config->min_level_write = CONFIG_USER_LEVEL_BASIC;
}

config_manager_result_t config_manager_init(const config_manager_config_t *config) {
    if (g_initialized) {
        ESP_LOGW(TAG, "Config Manager já inicializado");
        return CONFIG_MANAGER_OK;
    }
    
    ESP_LOGI(TAG, "Inicializando Config Manager v%s", CONFIG_MANAGER_VERSION);
    
    // Usar configuração padrão se não fornecida
    if (config) {
        g_config = *config;
    } else {
        config_manager_get_default_config(&g_config);
    }
    
    // Garantir que SPIFFS está montado
    esp_err_t ret = ensure_spiffs_mounted();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao montar SPIFFS");
        return CONFIG_MANAGER_ERROR_SPIFFS_NOT_MOUNTED;
    }
    
    // Criar diretórios se necessário
    config_manager_result_t result = ensure_directories();
    if (result != CONFIG_MANAGER_OK) {
        ESP_LOGE(TAG, "Falha ao criar diretórios");
        return result;
    }
    
    // Limpar arrays internos
    g_schema_count = 0;
    g_processor_count = 0;
    g_callback_count = 0;
    g_current_user_level = CONFIG_USER_LEVEL_NONE;
    
    g_initialized = true;
    
    ESP_LOGI(TAG, "Config Manager inicializado com sucesso");
    ESP_LOGI(TAG, "  Base path: %s", g_config.base_path);
    ESP_LOGI(TAG, "  Config dir: %s", g_config.config_dir);
    ESP_LOGI(TAG, "  NVS namespace: %s", g_config.nvs_namespace);
    ESP_LOGI(TAG, "  NVS backup: %s", g_config.enable_nvs_backup ? "enabled" : "disabled");
    ESP_LOGI(TAG, "  Validation: %s", g_config.enable_validation ? "enabled" : "disabled");
    
    return CONFIG_MANAGER_OK;
}

config_manager_result_t config_manager_deinit(void) {
    if (!g_initialized) {
        return CONFIG_MANAGER_OK;
    }
    
    ESP_LOGI(TAG, "Desinicializando Config Manager");
    
    // Desmontar SPIFFS (opcional)
    esp_vfs_spiffs_unregister(NULL);
    
    g_initialized = false;
    
    ESP_LOGI(TAG, "Config Manager desinicializado");
    return CONFIG_MANAGER_OK;
}

bool config_manager_is_initialized(void) {
    return g_initialized;
}

config_manager_result_t config_manager_save_json(const char *config_name, const cJSON *json_data) {
    if (!g_initialized) {
        return CONFIG_MANAGER_ERROR_NOT_INITIALIZED;
    }
    
    if (!config_name || !json_data) {
        return CONFIG_MANAGER_ERROR_INVALID_ARG;
    }
    
    // Verificar permissões
    if (!config_manager_check_access(config_name, true)) {
        ESP_LOGE(TAG, "Acesso negado para salvar %s", config_name);
        return CONFIG_MANAGER_ERROR_ACCESS_DENIED;
    }
    
    // Validar se habilitado
    if (g_config.enable_validation) {
        config_manager_result_t validation_result = config_manager_validate_json(config_name, json_data);
        if (validation_result != CONFIG_MANAGER_OK) {
            ESP_LOGE(TAG, "Validação falhou para %s", config_name);
            return validation_result;
        }
    }
    
    // Carregar configuração antiga para callback
    cJSON *old_data = NULL;
    config_manager_load_json(config_name, &old_data);
    
    // Gerar string JSON
    char *json_string = cJSON_Print(json_data);
    if (!json_string) {
        if (old_data) cJSON_Delete(old_data);
        return CONFIG_MANAGER_ERROR_OUT_OF_MEMORY;
    }
    
    // Verificar tamanho
    if (strlen(json_string) > g_config.max_file_size) {
        ESP_LOGE(TAG, "Arquivo %s muito grande: %d bytes > %d", 
                 config_name, strlen(json_string), g_config.max_file_size);
        free(json_string);
        if (old_data) cJSON_Delete(old_data);
        return CONFIG_MANAGER_ERROR_VALIDATION;
    }
    
    // Construir caminho do arquivo
    char file_path[CONFIG_MANAGER_MAX_PATH_LEN];
    build_config_path(config_name, file_path, sizeof(file_path));
    
    // Salvar no arquivo SPIFFS
    FILE *f = fopen(file_path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Erro ao abrir %s para escrita", file_path);
        free(json_string);
        if (old_data) cJSON_Delete(old_data);
        return CONFIG_MANAGER_ERROR_FILE_WRITE;
    }
    
    fprintf(f, "%s", json_string);
    fclose(f);
    
    ESP_LOGI(TAG, "Configuração %s salva em %s", config_name, file_path);
    
    // Backup na NVS
    if (g_config.enable_nvs_backup) {
        config_manager_result_t nvs_result = save_json_to_nvs(config_name, json_string);
        if (nvs_result != CONFIG_MANAGER_OK) {
            ESP_LOGW(TAG, "Falha no backup NVS para %s", config_name);
        }
    }
    
    // Executar processador se registrado
    void *user_data = NULL;
    config_processor_t processor = find_processor(config_name, &user_data);
    if (processor) {
        config_manager_result_t proc_result = processor(config_name, (cJSON*)json_data, user_data);
        if (proc_result != CONFIG_MANAGER_OK) {
            ESP_LOGW(TAG, "Processador para %s retornou erro: %d", config_name, proc_result);
        }
    }
    
    // Notificar callbacks
    notify_config_change(config_name, old_data, json_data);
    
    free(json_string);
    if (old_data) cJSON_Delete(old_data);
    
    return CONFIG_MANAGER_OK;
}

config_manager_result_t config_manager_load_json(const char *config_name, cJSON **json_data) {
    if (!g_initialized) {
        return CONFIG_MANAGER_ERROR_NOT_INITIALIZED;
    }
    
    if (!config_name || !json_data) {
        return CONFIG_MANAGER_ERROR_INVALID_ARG;
    }
    
    *json_data = NULL;
    
    // Verificar permissões
    if (!config_manager_check_access(config_name, false)) {
        ESP_LOGE(TAG, "Acesso negado para ler %s", config_name);
        return CONFIG_MANAGER_ERROR_ACCESS_DENIED;
    }
    
    char *file_content = NULL;
    bool loaded_from_nvs = false;
    
    // 1. Tentar carregar do arquivo SPIFFS
    char file_path[CONFIG_MANAGER_MAX_PATH_LEN];
    build_config_path(config_name, file_path, sizeof(file_path));
    
    FILE *f = fopen(file_path, "r");
    if (!f && g_config.enable_legacy_paths) {
        // Tentar caminho legacy
        build_legacy_path(config_name, file_path, sizeof(file_path));
        f = fopen(file_path, "r");
        if (f) {
            ESP_LOGI(TAG, "Carregando %s do caminho legacy: %s", config_name, file_path);
        }
    }
    
    if (f) {
        // Obter tamanho do arquivo
        fseek(f, 0, SEEK_END);
        long file_size = ftell(f);
        rewind(f);
        
        if (file_size > 0 && file_size <= g_config.max_file_size) {
            file_content = malloc(file_size + 1);
            if (file_content) {
                size_t read_size = fread(file_content, 1, file_size, f);
                file_content[read_size] = '\0';
                ESP_LOGD(TAG, "Configuração %s carregada do SPIFFS (%d bytes)", config_name, read_size);
            }
        }
        fclose(f);
    }
    
    // 2. Se falhou no SPIFFS, tentar NVS
    if (!file_content) {
        config_manager_result_t nvs_result = load_json_from_nvs(config_name, &file_content);
        if (nvs_result == CONFIG_MANAGER_OK) {
            loaded_from_nvs = true;
            ESP_LOGI(TAG, "Configuração %s recuperada da NVS", config_name);
        }
    }
    
    // 3. Se ambos falharam
    if (!file_content) {
        ESP_LOGD(TAG, "Configuração %s não encontrada", config_name);
        return CONFIG_MANAGER_ERROR_FILE_NOT_FOUND;
    }
    
    // Parsear JSON
    *json_data = cJSON_Parse(file_content);
    free(file_content);
    
    if (!*json_data) {
        ESP_LOGE(TAG, "Erro ao parsear JSON da configuração %s", config_name);
        return CONFIG_MANAGER_ERROR_JSON_PARSE;
    }
    
    // Se carregou da NVS, salvar no SPIFFS para sincronizar
    if (loaded_from_nvs) {
        ESP_LOGI(TAG, "Sincronizando %s da NVS para SPIFFS", config_name);
        config_manager_result_t save_result = config_manager_save_json(config_name, *json_data);
        if (save_result != CONFIG_MANAGER_OK) {
            ESP_LOGW(TAG, "Falha ao sincronizar %s para SPIFFS", config_name);
        }
    }
    
    ESP_LOGD(TAG, "Configuração %s carregada com sucesso", config_name);
    return CONFIG_MANAGER_OK;
}

config_manager_result_t config_manager_save_json_string(const char *config_name, const char *json_string) {
    if (!config_name || !json_string) {
        return CONFIG_MANAGER_ERROR_INVALID_ARG;
    }
    
    cJSON *json_data = cJSON_Parse(json_string);
    if (!json_data) {
        ESP_LOGE(TAG, "Erro ao parsear string JSON para %s", config_name);
        return CONFIG_MANAGER_ERROR_JSON_PARSE;
    }
    
    config_manager_result_t result = config_manager_save_json(config_name, json_data);
    cJSON_Delete(json_data);
    
    return result;
}

config_manager_result_t config_manager_load_json_string(const char *config_name, char **json_string) {
    if (!config_name || !json_string) {
        return CONFIG_MANAGER_ERROR_INVALID_ARG;
    }
    
    cJSON *json_data = NULL;
    config_manager_result_t result = config_manager_load_json(config_name, &json_data);
    
    if (result == CONFIG_MANAGER_OK) {
        *json_string = cJSON_Print(json_data);
        if (!*json_string) {
            result = CONFIG_MANAGER_ERROR_OUT_OF_MEMORY;
        }
        cJSON_Delete(json_data);
    } else {
        *json_string = NULL;
    }
    
    return result;
}

bool config_manager_exists(const char *config_name) {
    if (!g_initialized || !config_name) {
        return false;
    }
    
    char file_path[CONFIG_MANAGER_MAX_PATH_LEN];
    build_config_path(config_name, file_path, sizeof(file_path));
    
    struct stat st;
    if (stat(file_path, &st) == 0) {
        return true;
    }
    
    // Verificar caminho legacy
    if (g_config.enable_legacy_paths) {
        build_legacy_path(config_name, file_path, sizeof(file_path));
        if (stat(file_path, &st) == 0) {
            return true;
        }
    }
    
    return false;
}

config_manager_result_t config_manager_delete(const char *config_name) {
    if (!g_initialized) {
        return CONFIG_MANAGER_ERROR_NOT_INITIALIZED;
    }
    
    if (!config_name) {
        return CONFIG_MANAGER_ERROR_INVALID_ARG;
    }
    
    // Verificar permissões
    if (!config_manager_check_access(config_name, true)) {
        ESP_LOGE(TAG, "Acesso negado para excluir %s", config_name);
        return CONFIG_MANAGER_ERROR_ACCESS_DENIED;
    }
    
    // Carregar dados atuais para callback
    cJSON *old_data = NULL;
    config_manager_load_json(config_name, &old_data);
    
    // Excluir arquivo SPIFFS
    char file_path[CONFIG_MANAGER_MAX_PATH_LEN];
    build_config_path(config_name, file_path, sizeof(file_path));
    
    if (unlink(file_path) != 0) {
        ESP_LOGW(TAG, "Falha ao excluir %s", file_path);
    } else {
        ESP_LOGI(TAG, "Configuração %s excluída", config_name);
    }
    
    // Excluir backup NVS
    if (g_config.enable_nvs_backup) {
        config_manager_clear_nvs_backup(config_name);
    }
    
    // Notificar callbacks
    notify_config_change(config_name, old_data, NULL);
    
    if (old_data) cJSON_Delete(old_data);
    
    return CONFIG_MANAGER_OK;
}

config_manager_result_t config_manager_validate_json(const char *config_name, const cJSON *json_data) {
    if (!g_initialized || !config_name || !json_data) {
        return CONFIG_MANAGER_ERROR_INVALID_ARG;
    }
    
    if (!g_config.enable_validation) {
        return CONFIG_MANAGER_OK; // Validação desabilitada
    }
    
    const config_schema_t *schema = find_schema(config_name);
    if (!schema) {
        ESP_LOGD(TAG, "Schema não encontrado para %s, pulando validação", config_name);
        return CONFIG_MANAGER_OK; // Sem schema = sem validação
    }
    
    ESP_LOGD(TAG, "Validando %s com %d campos", config_name, schema->field_count);
    
    // Verificar cada campo do schema
    for (size_t i = 0; i < schema->field_count; i++) {
        const config_field_validator_t *field = &schema->fields[i];
        
        cJSON *value = cJSON_GetObjectItem(json_data, field->field_name);
        
        if (!value) {
            if (field->required) {
                ESP_LOGE(TAG, "Campo obrigatório %s não encontrado em %s", field->field_name, config_name);
                return CONFIG_MANAGER_ERROR_VALIDATION;
            }
            continue; // Campo opcional ausente
        }
        
        config_manager_result_t field_result = validate_field(field, value);
        if (field_result != CONFIG_MANAGER_OK) {
            ESP_LOGE(TAG, "Validação falhou para campo %s em %s", field->field_name, config_name);
            return field_result;
        }
    }
    
    ESP_LOGD(TAG, "Validação de %s concluída com sucesso", config_name);
    return CONFIG_MANAGER_OK;
}

config_manager_result_t config_manager_register_schema(const config_schema_t *schema) {
    if (!g_initialized || !schema || g_schema_count >= MAX_SCHEMAS) {
        return CONFIG_MANAGER_ERROR_INVALID_ARG;
    }
    
    // Verificar se já existe
    if (find_schema(schema->config_name)) {
        ESP_LOGW(TAG, "Schema para %s já registrado, substituindo", schema->config_name);
        // Substituir existente
        for (size_t i = 0; i < g_schema_count; i++) {
            if (strcmp(g_schemas[i].config_name, schema->config_name) == 0) {
                g_schemas[i] = *schema;
                ESP_LOGI(TAG, "Schema %s atualizado", schema->config_name);
                return CONFIG_MANAGER_OK;
            }
        }
    }
    
    // Adicionar novo
    g_schemas[g_schema_count] = *schema;
    g_schema_count++;
    
    ESP_LOGI(TAG, "Schema %s registrado com %d campos", schema->config_name, schema->field_count);
    return CONFIG_MANAGER_OK;
}

const config_schema_t* config_manager_get_schema(const char *config_name) {
    if (!g_initialized || !config_name) {
        return NULL;
    }
    
    return find_schema(config_name);
}

// ============================================================================
// SISTEMA DE USUÁRIOS
// ============================================================================

void config_manager_set_user_level(config_user_level_t level) {
    g_current_user_level = level;
    ESP_LOGD(TAG, "Nível de usuário definido: %d", level);
}

config_user_level_t config_manager_get_user_level(void) {
    return g_current_user_level;
}

bool config_manager_check_access(const char *config_name, bool write_access) {
    if (!g_initialized) {
        return false;
    }
    
    config_user_level_t required_level = write_access ? g_config.min_level_write : g_config.min_level_read;
    
    // Verificar schema específico
    const config_schema_t *schema = find_schema(config_name);
    if (schema && write_access) {
        required_level = schema->required_level;
    }
    
    bool has_access = g_current_user_level >= required_level;
    
    ESP_LOGD(TAG, "Verificação de acesso %s %s: nível %d >= %d ? %s", 
             config_name, write_access ? "escrita" : "leitura",
             g_current_user_level, required_level, has_access ? "OK" : "NEGADO");
    
    return has_access;
}

void config_manager_save_login_state(bool logged_in, config_user_level_t level) {
    nvs_handle_t handle;
    if (nvs_open("auth", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, "logged_in", logged_in ? 1 : 0);
        nvs_set_u8(handle, "user_level", (uint8_t)level);
        nvs_commit(handle);
        nvs_close(handle);
        
        ESP_LOGI(TAG, "Estado de login salvo: %s, nível %d", 
                 logged_in ? "logado" : "deslogado", level);
        
        // Atualizar estado atual
        if (logged_in) {
            config_manager_set_user_level(level);
        } else {
            config_manager_set_user_level(CONFIG_USER_LEVEL_NONE);
        }
    }
}

void config_manager_load_login_state(bool *logged_in, config_user_level_t *level) {
    nvs_handle_t handle;
    if (nvs_open("auth", NVS_READONLY, &handle) == ESP_OK) {
        uint8_t login_state = 0, user_level = 0;
        
        nvs_get_u8(handle, "logged_in", &login_state);
        nvs_get_u8(handle, "user_level", &user_level);
        
        nvs_close(handle);
        
        if (logged_in) *logged_in = (login_state == 1);
        if (level) *level = (config_user_level_t)user_level;
        
        // Atualizar estado atual
        if (login_state == 1) {
            config_manager_set_user_level((config_user_level_t)user_level);
        }
        
        ESP_LOGD(TAG, "Estado de login carregado: %s, nível %d", 
                 (login_state == 1) ? "logado" : "deslogado", user_level);
    } else {
        if (logged_in) *logged_in = false;
        if (level) *level = CONFIG_USER_LEVEL_NONE;
    }
}

// ============================================================================
// UTILITÁRIOS
// ============================================================================

const char* config_manager_get_error_string(config_manager_result_t result) {
    switch (result) {
        case CONFIG_MANAGER_OK:
            return "Sucesso";
        case CONFIG_MANAGER_ERROR_INVALID_ARG:
            return "Argumento inválido";
        case CONFIG_MANAGER_ERROR_FILE_NOT_FOUND:
            return "Arquivo não encontrado";
        case CONFIG_MANAGER_ERROR_JSON_PARSE:
            return "Erro ao parsear JSON";
        case CONFIG_MANAGER_ERROR_FILE_WRITE:
            return "Erro ao escrever arquivo";
        case CONFIG_MANAGER_ERROR_FILE_READ:
            return "Erro ao ler arquivo";
        case CONFIG_MANAGER_ERROR_NVS:
            return "Erro na NVS";
        case CONFIG_MANAGER_ERROR_OUT_OF_MEMORY:
            return "Sem memória";
        case CONFIG_MANAGER_ERROR_VALIDATION:
            return "Erro na validação";
        case CONFIG_MANAGER_ERROR_NOT_INITIALIZED:
            return "Biblioteca não inicializada";
        case CONFIG_MANAGER_ERROR_ACCESS_DENIED:
            return "Acesso negado";
        case CONFIG_MANAGER_ERROR_CONFIG_EXISTS:
            return "Configuração já existe";
        case CONFIG_MANAGER_ERROR_SPIFFS_NOT_MOUNTED:
            return "SPIFFS não montado";
        default:
            return "Erro desconhecido";
    }
}

esp_err_t config_manager_result_to_esp_err(config_manager_result_t result) {
    switch (result) {
        case CONFIG_MANAGER_OK:
            return ESP_OK;
        case CONFIG_MANAGER_ERROR_INVALID_ARG:
            return ESP_ERR_INVALID_ARG;
        case CONFIG_MANAGER_ERROR_FILE_NOT_FOUND:
            return ESP_ERR_NOT_FOUND;
        case CONFIG_MANAGER_ERROR_OUT_OF_MEMORY:
            return ESP_ERR_NO_MEM;
        case CONFIG_MANAGER_ERROR_NOT_INITIALIZED:
            return ESP_ERR_INVALID_STATE;
        case CONFIG_MANAGER_ERROR_SPIFFS_NOT_MOUNTED:
            return ESP_ERR_INVALID_STATE;
        default:
            return ESP_FAIL;
    }
}

config_manager_result_t config_manager_ensure_config_dirs(void) {
    if (!g_initialized) {
        return CONFIG_MANAGER_ERROR_NOT_INITIALIZED;
    }
    
    return ensure_directories();
}

config_manager_result_t config_manager_backup_to_nvs(const char *config_name) {
    if (!g_initialized || !config_name) {
        return CONFIG_MANAGER_ERROR_INVALID_ARG;
    }
    
    char *json_string = NULL;
    config_manager_result_t result = config_manager_load_json_string(config_name, &json_string);
    
    if (result == CONFIG_MANAGER_OK) {
        result = save_json_to_nvs(config_name, json_string);
        free(json_string);
        
        if (result == CONFIG_MANAGER_OK) {
            ESP_LOGI(TAG, "Backup %s criado na NVS", config_name);
        }
    }
    
    return result;
}

config_manager_result_t config_manager_restore_from_nvs(const char *config_name, bool overwrite_existing) {
    if (!g_initialized || !config_name) {
        return CONFIG_MANAGER_ERROR_INVALID_ARG;
    }
    
    // Verificar se já existe e se deve sobrescrever
    if (!overwrite_existing && config_manager_exists(config_name)) {
        ESP_LOGW(TAG, "Configuração %s já existe, pulando restauração", config_name);
        return CONFIG_MANAGER_ERROR_CONFIG_EXISTS;
    }
    
    char *json_string = NULL;
    config_manager_result_t result = load_json_from_nvs(config_name, &json_string);
    
    if (result == CONFIG_MANAGER_OK) {
        result = config_manager_save_json_string(config_name, json_string);
        free(json_string);
        
        if (result == CONFIG_MANAGER_OK) {
            ESP_LOGI(TAG, "Configuração %s restaurada da NVS", config_name);
        }
    }
    
    return result;
}

config_manager_result_t config_manager_clear_nvs_backup(const char *config_name) {
    if (!g_initialized || !config_name) {
        return CONFIG_MANAGER_ERROR_INVALID_ARG;
    }
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(g_config.nvs_namespace, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return CONFIG_MANAGER_ERROR_NVS;
    }
    
    err = nvs_erase_key(nvs_handle, config_name);
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_commit(nvs_handle);
        ESP_LOGI(TAG, "Backup %s removido da NVS", config_name);
    }
    
    nvs_close(nvs_handle);
    
    return (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) ? CONFIG_MANAGER_OK : CONFIG_MANAGER_ERROR_NVS;
}

// Implementação básica das outras funções seria muito longa,
// mas estas são as principais funcionalidades da biblioteca