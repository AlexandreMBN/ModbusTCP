#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "config_manager.h"

static const char *TAG = "CONFIG_EXAMPLE";

// ============================================================================
// SCHEMAS DE VALIDAÇÃO
// ============================================================================

// Schema para configuração WiFi AP
static const char *ap_channel_values[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", NULL};
static const double ap_max_conn_min = 1, ap_max_conn_max = 8;

static const config_field_validator_t ap_fields[] = {
    {"ssid", CONFIG_TYPE_STRING, true, NULL, NULL, NULL, NULL},
    {"password", CONFIG_TYPE_STRING, true, NULL, NULL, NULL, NULL},
    {"ip", CONFIG_TYPE_STRING, true, NULL, NULL, "^\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}$", NULL},
    {"channel", CONFIG_TYPE_INTEGER, false, NULL, NULL, NULL, ap_channel_values},
    {"max_connections", CONFIG_TYPE_INTEGER, false, &ap_max_conn_min, &ap_max_conn_max, NULL, NULL},
    {"hidden", CONFIG_TYPE_BOOLEAN, false, NULL, NULL, NULL, NULL}
};

static const config_schema_t ap_schema = {
    .config_name = "ap_config",
    .fields = ap_fields,
    .field_count = sizeof(ap_fields) / sizeof(ap_fields[0]),
    .required_level = CONFIG_USER_LEVEL_ADMIN
};

// Schema para configuração MQTT
static const double mqtt_port_min = 1, mqtt_port_max = 65535;
static const double mqtt_qos_min = 0, mqtt_qos_max = 2;

static const config_field_validator_t mqtt_fields[] = {
    {"broker_url", CONFIG_TYPE_STRING, true, NULL, NULL, NULL, NULL},
    {"client_id", CONFIG_TYPE_STRING, true, NULL, NULL, NULL, NULL},
    {"port", CONFIG_TYPE_INTEGER, false, &mqtt_port_min, &mqtt_port_max, NULL, NULL},
    {"qos", CONFIG_TYPE_INTEGER, false, &mqtt_qos_min, &mqtt_qos_max, NULL, NULL},
    {"enabled", CONFIG_TYPE_BOOLEAN, false, NULL, NULL, NULL, NULL},
    {"tls_enabled", CONFIG_TYPE_BOOLEAN, false, NULL, NULL, NULL, NULL}
};

static const config_schema_t mqtt_schema = {
    .config_name = "mqtt_config",
    .fields = mqtt_fields,
    .field_count = sizeof(mqtt_fields) / sizeof(mqtt_fields[0]),
    .required_level = CONFIG_USER_LEVEL_BASIC
};

// ============================================================================
// CALLBACKS E PROCESSADORES
// ============================================================================

// Callback para mudanças de configuração
void config_change_handler(const char *config_name, 
                          const cJSON *old_data, 
                          const cJSON *new_data, 
                          void *user_data) {
    ESP_LOGI(TAG, "🔄 Configuração %s foi alterada!", config_name);
    
    if (strcmp(config_name, "wifi_config") == 0) {
        ESP_LOGI(TAG, "  📶 WiFi será reiniciado com novas configurações");
        // Aqui você reiniciaria o WiFi
    } else if (strcmp(config_name, "mqtt_config") == 0) {
        ESP_LOGI(TAG, "  📡 Cliente MQTT será reconectado");
        // Aqui você reconectaria o MQTT
    }
}

// Processador para configuração AP
config_manager_result_t ap_config_processor(const char *config_name, 
                                           cJSON *json_data, 
                                           void *user_data) {
    ESP_LOGI(TAG, "🔧 Processando configuração AP...");
    
    // Exemplo: garantir que senha tem tamanho mínimo
    cJSON *password = cJSON_GetObjectItem(json_data, "password");
    if (password && cJSON_IsString(password)) {
        const char *pwd = cJSON_GetStringValue(password);
        if (strlen(pwd) < 8) {
            ESP_LOGE(TAG, "Senha AP deve ter pelo menos 8 caracteres");
            return CONFIG_MANAGER_ERROR_VALIDATION;
        }
    }
    
    // Exemplo: definir canal padrão se não especificado
    cJSON *channel = cJSON_GetObjectItem(json_data, "channel");
    if (!channel) {
        cJSON_AddNumberToObject(json_data, "channel", 6);
        ESP_LOGI(TAG, "Canal AP definido para 6 (padrão)");
    }
    
    ESP_LOGI(TAG, "✅ Processamento AP concluído");
    return CONFIG_MANAGER_OK;
}

// ============================================================================
// EXEMPLOS DE USO
// ============================================================================

static void example_basic_usage(void) {
    ESP_LOGI(TAG, "=== Exemplo 1: Uso Básico ===");
    
    // 1. Criar configuração AP manualmente
    cJSON *ap_config = cJSON_CreateObject();
    cJSON_AddStringToObject(ap_config, "ssid", "ESP32-Device");
    cJSON_AddStringToObject(ap_config, "password", "12345678");
    cJSON_AddStringToObject(ap_config, "ip", "192.168.4.1");
    cJSON_AddNumberToObject(ap_config, "channel", 6);
    cJSON_AddNumberToObject(ap_config, "max_connections", 4);
    cJSON_AddBoolToObject(ap_config, "hidden", false);
    
    // 2. Salvar configuração
    config_manager_result_t result = config_manager_save_json("ap_config", ap_config);
    if (result == CONFIG_MANAGER_OK) {
        ESP_LOGI(TAG, "✅ Configuração AP salva com sucesso");
    } else {
        ESP_LOGE(TAG, "❌ Erro ao salvar AP: %s", config_manager_get_error_string(result));
    }
    
    cJSON_Delete(ap_config);
    
    // 3. Carregar configuração
    cJSON *loaded_config = NULL;
    result = config_manager_load_json("ap_config", &loaded_config);
    if (result == CONFIG_MANAGER_OK) {
        ESP_LOGI(TAG, "✅ Configuração AP carregada:");
        
        char *config_str = cJSON_Print(loaded_config);
        ESP_LOGI(TAG, "%s", config_str);
        free(config_str);
        
        cJSON_Delete(loaded_config);
    } else {
        ESP_LOGE(TAG, "❌ Erro ao carregar AP: %s", config_manager_get_error_string(result));
    }
}

static void example_validation_system(void) {
    ESP_LOGI(TAG, "\\n=== Exemplo 2: Sistema de Validação ===");
    
    // 1. Registrar schemas
    config_manager_register_schema(&ap_schema);
    config_manager_register_schema(&mqtt_schema);
    ESP_LOGI(TAG, "📋 Schemas de validação registrados");
    
    // 2. Tentar salvar configuração inválida
    cJSON *invalid_config = cJSON_CreateObject();
    cJSON_AddStringToObject(invalid_config, "ssid", ""); // SSID vazio (inválido)
    cJSON_AddStringToObject(invalid_config, "password", "123"); // Senha muito curta
    cJSON_AddStringToObject(invalid_config, "ip", "invalid-ip"); // IP inválido
    
    config_manager_result_t result = config_manager_save_json("ap_config", invalid_config);
    if (result != CONFIG_MANAGER_OK) {
        ESP_LOGI(TAG, "✅ Validação funcionou - configuração inválida rejeitada: %s", 
                 config_manager_get_error_string(result));
    }
    
    cJSON_Delete(invalid_config);
    
    // 3. Salvar configuração válida
    cJSON *valid_config = cJSON_CreateObject();
    cJSON_AddStringToObject(valid_config, "ssid", "ESP32-ValidAP");
    cJSON_AddStringToObject(valid_config, "password", "validpassword123");
    cJSON_AddStringToObject(valid_config, "ip", "192.168.4.1");
    cJSON_AddNumberToObject(valid_config, "channel", 11);
    cJSON_AddNumberToObject(valid_config, "max_connections", 6);
    cJSON_AddBoolToObject(valid_config, "hidden", false);
    
    result = config_manager_save_json("ap_config", valid_config);
    if (result == CONFIG_MANAGER_OK) {
        ESP_LOGI(TAG, "✅ Configuração válida salva com sucesso");
    }
    
    cJSON_Delete(valid_config);
}

static void example_access_control(void) {
    ESP_LOGI(TAG, "\\n=== Exemplo 3: Controle de Acesso ===");
    
    // 1. Tentar salvar sem permissão
    config_manager_set_user_level(CONFIG_USER_LEVEL_NONE);
    
    cJSON *test_config = cJSON_CreateObject();
    cJSON_AddStringToObject(test_config, "test", "value");
    
    config_manager_result_t result = config_manager_save_json("test_config", test_config);
    if (result == CONFIG_MANAGER_ERROR_ACCESS_DENIED) {
        ESP_LOGI(TAG, "✅ Controle de acesso funcionou - acesso negado para usuário não logado");
    }
    
    // 2. Fazer login como admin
    config_manager_set_user_level(CONFIG_USER_LEVEL_ADMIN);
    ESP_LOGI(TAG, "🔐 Login como administrador");
    
    result = config_manager_save_json("test_config", test_config);
    if (result == CONFIG_MANAGER_OK) {
        ESP_LOGI(TAG, "✅ Configuração salva com sucesso como admin");
    }
    
    // 3. Salvar estado de login
    config_manager_save_login_state(true, CONFIG_USER_LEVEL_ADMIN);
    ESP_LOGI(TAG, "💾 Estado de login salvo");
    
    cJSON_Delete(test_config);
}

static void example_backup_recovery(void) {
    ESP_LOGI(TAG, "\\n=== Exemplo 4: Backup e Recuperação ===");
    
    // 1. Criar configuração MQTT
    cJSON *mqtt_config = cJSON_CreateObject();
    cJSON_AddStringToObject(mqtt_config, "broker_url", "mqtt://192.168.1.100");
    cJSON_AddStringToObject(mqtt_config, "client_id", "esp32_device_001");
    cJSON_AddStringToObject(mqtt_config, "username", "user");
    cJSON_AddStringToObject(mqtt_config, "password", "pass");
    cJSON_AddNumberToObject(mqtt_config, "port", 1883);
    cJSON_AddNumberToObject(mqtt_config, "qos", 1);
    cJSON_AddBoolToObject(mqtt_config, "enabled", true);
    cJSON_AddBoolToObject(mqtt_config, "tls_enabled", false);
    
    // 2. Salvar (backup automático na NVS)
    config_manager_result_t result = config_manager_save_json("mqtt_config", mqtt_config);
    if (result == CONFIG_MANAGER_OK) {
        ESP_LOGI(TAG, "✅ Configuração MQTT salva (backup automático na NVS)");
    }
    
    cJSON_Delete(mqtt_config);
    
    // 3. Simular perda do arquivo SPIFFS
    ESP_LOGI(TAG, "🗑️ Simulando exclusão do arquivo...");
    config_manager_delete("mqtt_config");
    
    // 4. Verificar que não existe mais
    if (!config_manager_exists("mqtt_config")) {
        ESP_LOGI(TAG, "❌ Configuração não existe mais no SPIFFS");
    }
    
    // 5. Restaurar da NVS
    ESP_LOGI(TAG, "🔄 Restaurando da NVS...");
    result = config_manager_restore_from_nvs("mqtt_config", false);
    if (result == CONFIG_MANAGER_OK) {
        ESP_LOGI(TAG, "✅ Configuração restaurada da NVS com sucesso!");
        
        // Verificar conteúdo
        cJSON *restored = NULL;
        if (config_manager_load_json("mqtt_config", &restored) == CONFIG_MANAGER_OK) {
            char *json_str = cJSON_Print(restored);
            ESP_LOGI(TAG, "📄 Conteúdo restaurado: %s", json_str);
            free(json_str);
            cJSON_Delete(restored);
        }
    }
}

static void example_callbacks_processors(void) {
    ESP_LOGI(TAG, "\\n=== Exemplo 5: Callbacks e Processadores ===");
    
    // 1. Registrar callback global (todas as configurações)
    config_manager_register_change_callback(NULL, config_change_handler, NULL);
    ESP_LOGI(TAG, "📞 Callback global registrado");
    
    // 2. Registrar processador para AP
    config_manager_register_processor("ap_config", ap_config_processor, NULL);
    ESP_LOGI(TAG, "⚙️ Processador AP registrado");
    
    // 3. Salvar configuração que disparará callback e processador
    cJSON *ap_config = cJSON_CreateObject();
    cJSON_AddStringToObject(ap_config, "ssid", "ESP32-CallbackTest");
    cJSON_AddStringToObject(ap_config, "password", "testpassword");
    cJSON_AddStringToObject(ap_config, "ip", "192.168.4.1");
    // Note: não definimos channel, será definido pelo processador
    
    config_manager_result_t result = config_manager_save_json("ap_config", ap_config);
    if (result == CONFIG_MANAGER_OK) {
        ESP_LOGI(TAG, "✅ Configuração salva - callbacks e processadores executados");
    }
    
    cJSON_Delete(ap_config);
    
    // 4. Verificar se processador adicionou o canal
    cJSON *processed = NULL;
    if (config_manager_load_json("ap_config", &processed) == CONFIG_MANAGER_OK) {
        cJSON *channel = cJSON_GetObjectItem(processed, "channel");
        if (channel && cJSON_IsNumber(channel)) {
            ESP_LOGI(TAG, "✅ Processador funcionou - canal adicionado: %d", (int)cJSON_GetNumberValue(channel));
        }
        cJSON_Delete(processed);
    }
}

static void example_compatibility_api(void) {
    ESP_LOGI(TAG, "\\n=== Exemplo 6: API de Compatibilidade ===");
    
    // 1. Usar API de alto nível para configuração AP
    config_ap_t ap_config = {
        .ssid = "ESP32-HighLevel",
        .password = "password123",
        .ip = "192.168.4.1",
        .username = "admin",
        .channel = 8,
        .max_connections = 5,
        .hidden = false
    };
    
    config_manager_result_t result = config_manager_save_ap_config(&ap_config);
    if (result == CONFIG_MANAGER_OK) {
        ESP_LOGI(TAG, "✅ Configuração AP salva via API de alto nível");
    }
    
    // 2. Carregar usando API de alto nível
    config_ap_t loaded_ap = {0};
    result = config_manager_load_ap_config(&loaded_ap);
    if (result == CONFIG_MANAGER_OK) {
        ESP_LOGI(TAG, "✅ Configuração AP carregada:");
        ESP_LOGI(TAG, "  SSID: %s", loaded_ap.ssid);
        ESP_LOGI(TAG, "  IP: %s", loaded_ap.ip);
        ESP_LOGI(TAG, "  Canal: %d", loaded_ap.channel);
        ESP_LOGI(TAG, "  Max Conexões: %d", loaded_ap.max_connections);
    }
    
    // 3. Configuração MQTT de alto nível
    config_mqtt_t mqtt_config = {
        .broker_url = "mqtt://test.mosquitto.org",
        .client_id = "esp32_highlevel_test",
        .username = "",
        .password = "",
        .port = 1883,
        .qos = 1,
        .retain = false,
        .tls_enabled = false,
        .ca_path = "",
        .enabled = true,
        .publish_interval_ms = 5000,
        .keep_alive = 60,
        .clean_session = true
    };
    
    result = config_manager_save_mqtt_config(&mqtt_config);
    if (result == CONFIG_MANAGER_OK) {
        ESP_LOGI(TAG, "✅ Configuração MQTT salva via API de alto nível");
    }
}

// ============================================================================
// FUNÇÃO PRINCIPAL DO EXEMPLO
// ============================================================================

void config_manager_example(void) {
    ESP_LOGI(TAG, "🚀 Iniciando exemplos do Config Manager");
    
    // Inicializar biblioteca
    config_manager_config_t config;
    config_manager_get_default_config(&config);
    
    // Personalizar configuração
    config.enable_validation = true;
    config.enable_nvs_backup = true;
    config.auto_create_dirs = true;
    config.min_level_write = CONFIG_USER_LEVEL_BASIC;
    
    config_manager_result_t result = config_manager_init(&config);
    if (result != CONFIG_MANAGER_OK) {
        ESP_LOGE(TAG, "❌ Falha ao inicializar Config Manager: %s", 
                 config_manager_get_error_string(result));
        return;
    }
    
    ESP_LOGI(TAG, "✅ Config Manager inicializado com sucesso\\n");
    
    // Executar exemplos
    example_basic_usage();
    example_validation_system();
    example_access_control();
    example_backup_recovery();
    example_callbacks_processors();
    example_compatibility_api();
    
    // Informações do sistema
    size_t total_configs, nvs_backups, memory_used;
    if (config_manager_get_system_info(&total_configs, &nvs_backups, &memory_used) == CONFIG_MANAGER_OK) {
        ESP_LOGI(TAG, "\\n📊 Informações do sistema:");
        ESP_LOGI(TAG, "  Configurações: %d", total_configs);
        ESP_LOGI(TAG, "  Backups NVS: %d", nvs_backups);
        ESP_LOGI(TAG, "  Memória usada: ~%d bytes", memory_used);
    }
    
    ESP_LOGI(TAG, "🎯 Todos os exemplos executados com sucesso!");
}

void config_manager_cleanup_example(void) {
    ESP_LOGI(TAG, "🧹 Limpando recursos do exemplo");
    
    // Opcional: limpar configurações de teste
    config_manager_delete("test_config");
    
    // Desinicializar
    config_manager_deinit();
    
    ESP_LOGI(TAG, "✅ Limpeza concluída");
}