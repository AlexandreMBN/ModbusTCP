# Config Manager

Uma biblioteca completa e robusta para gerenciamento de configurações JSON no ESP32. Projetada para oferecer máxima flexibilidade, segurança e facilidade de uso em projetos IoT profissionais.

## 🌟 Características Principais

- **🗃️ Gerenciamento Unificado**: API consistente para todas as configurações
- **✅ Validação Automática**: Sistema de schemas com validação de tipos e valores
- **💾 Backup Inteligente**: Backup automático em NVS com recuperação de falhas
- **🔐 Sistema de Usuários**: Controle de acesso com níveis de permissão
- **📞 Callbacks Dinâmicos**: Notificações automáticas de mudanças
- **⚙️ Processadores Customizados**: Processamento automático antes de salvar
- **🔄 Migração Automática**: Compatibilidade com formatos antigos
- **📊 Diagnósticos Completos**: Monitoramento e relatórios do sistema

## 📦 Instalação

### PlatformIO
```bash
# Copie a pasta lib/config_manager para seu projeto
# A biblioteca será detectada automaticamente
```

### ESP-IDF
```cmake
# Adicione no CMakeLists.txt principal:
set(EXTRA_COMPONENT_DIRS "lib/config_manager")
```

## 🚀 Uso Rápido

### Inicialização Básica

```c
#include "config_manager.h"

void app_main() {
    // Configuração padrão
    config_manager_config_t config;
    config_manager_get_default_config(&config);
    
    // Customizar se necessário
    config.enable_validation = true;
    config.enable_nvs_backup = true;
    
    // Inicializar
    config_manager_result_t result = config_manager_init(&config);
    if (result == CONFIG_MANAGER_OK) {
        ESP_LOGI("APP", "Config Manager pronto!");
    }
}
```

### Salvando Configurações

```c
// Método 1: JSON direto
cJSON *wifi_config = cJSON_CreateObject();
cJSON_AddStringToObject(wifi_config, "ssid", "MeuWiFi");
cJSON_AddStringToObject(wifi_config, "password", "minhasenha");
cJSON_AddBoolToObject(wifi_config, "enabled", true);

config_manager_save_json("wifi_config", wifi_config);
cJSON_Delete(wifi_config);

// Método 2: String JSON
const char *json_str = "{\"broker\":\"mqtt://192.168.1.100\",\"port\":1883}";
config_manager_save_json_string("mqtt_config", json_str);

// Método 3: API de alto nível
config_ap_t ap_config = {
    .ssid = "ESP32-Device",
    .password = "12345678",
    .ip = "192.168.4.1",
    .channel = 6
};
config_manager_save_ap_config(&ap_config);
```

### Carregando Configurações

```c
// Método 1: JSON
cJSON *config = NULL;
if (config_manager_load_json("wifi_config", &config) == CONFIG_MANAGER_OK) {
    cJSON *ssid = cJSON_GetObjectItem(config, "ssid");
    if (cJSON_IsString(ssid)) {
        ESP_LOGI("APP", "SSID: %s", cJSON_GetStringValue(ssid));
    }
    cJSON_Delete(config);
}

// Método 2: API de alto nível
config_ap_t ap_config;
if (config_manager_load_ap_config(&ap_config) == CONFIG_MANAGER_OK) {
    ESP_LOGI("APP", "AP SSID: %s, IP: %s", ap_config.ssid, ap_config.ip);
}
```

## ⚙️ Sistema de Validação

### Definindo Schemas

```c
// Validadores para configuração WiFi
static const config_field_validator_t wifi_fields[] = {
    {"ssid", CONFIG_TYPE_STRING, true, NULL, NULL, NULL, NULL},
    {"password", CONFIG_TYPE_STRING, true, NULL, NULL, NULL, NULL},
    {"enabled", CONFIG_TYPE_BOOLEAN, false, NULL, NULL, NULL, NULL}
};

static const config_schema_t wifi_schema = {
    .config_name = "wifi_config",
    .fields = wifi_fields,
    .field_count = 3,
    .required_level = CONFIG_USER_LEVEL_ADMIN
};

// Registrar schema
config_manager_register_schema(&wifi_schema);
```

### Validação com Valores Permitidos

```c
// Valores permitidos para canal WiFi
static const char *wifi_channels[] = {"1", "6", "11", NULL};

static const config_field_validator_t ap_fields[] = {
    {"ssid", CONFIG_TYPE_STRING, true, NULL, NULL, NULL, NULL},
    {"channel", CONFIG_TYPE_INTEGER, false, NULL, NULL, NULL, wifi_channels}
};
```

### Validação com Limites

```c
// Limites para porta MQTT
static const double port_min = 1, port_max = 65535;

static const config_field_validator_t mqtt_fields[] = {
    {"broker", CONFIG_TYPE_STRING, true, NULL, NULL, NULL, NULL},
    {"port", CONFIG_TYPE_INTEGER, false, &port_min, &port_max, NULL, NULL}
};
```

## 🔐 Sistema de Usuários

### Níveis de Acesso

```c
typedef enum {
    CONFIG_USER_LEVEL_NONE = 0,     // Não logado
    CONFIG_USER_LEVEL_BASIC = 1,    // Usuário básico
    CONFIG_USER_LEVEL_ADMIN = 2,    // Administrador
    CONFIG_USER_LEVEL_ROOT = 3      // Super administrador
} config_user_level_t;
```

### Controle de Acesso

```c
// Definir nível do usuário
config_manager_set_user_level(CONFIG_USER_LEVEL_ADMIN);

// Verificar permissões
if (config_manager_check_access("system_config", true)) {
    // Usuário pode modificar configuração do sistema
}

// Salvar estado de login
config_manager_save_login_state(true, CONFIG_USER_LEVEL_ADMIN);
```

## 📞 Callbacks e Processadores

### Callbacks para Mudanças

```c
void config_change_handler(const char *config_name, 
                          const cJSON *old_data, 
                          const cJSON *new_data, 
                          void *user_data) {
    ESP_LOGI("APP", "Configuração %s alterada!", config_name);
    
    if (strcmp(config_name, "wifi_config") == 0) {
        // Reiniciar WiFi com nova configuração
        restart_wifi();
    }
}

// Registrar callback global (todas as configurações)
config_manager_register_change_callback(NULL, config_change_handler, NULL);

// Registrar callback específico
config_manager_register_change_callback("mqtt_config", mqtt_change_handler, NULL);
```

### Processadores Customizados

```c
config_manager_result_t wifi_processor(const char *config_name, 
                                      cJSON *json_data, 
                                      void *user_data) {
    // Validar senha WiFi
    cJSON *password = cJSON_GetObjectItem(json_data, "password");
    if (password && cJSON_IsString(password)) {
        const char *pwd = cJSON_GetStringValue(password);
        if (strlen(pwd) < 8) {
            ESP_LOGE("WIFI", "Senha deve ter pelo menos 8 caracteres");
            return CONFIG_MANAGER_ERROR_VALIDATION;
        }
    }
    
    // Adicionar timestamp
    cJSON_AddNumberToObject(json_data, "last_updated", time(NULL));
    
    return CONFIG_MANAGER_OK;
}

// Registrar processador
config_manager_register_processor("wifi_config", wifi_processor, NULL);
```

## 💾 Backup e Recuperação

### Backup Automático

```c
// O backup é automático quando habilitado na configuração
config_manager_config_t config;
config_manager_get_default_config(&config);
config.enable_nvs_backup = true;  // Backup automático
config_manager_init(&config);

// Agora todas as operações de save fazem backup automático na NVS
```

### Backup Manual

```c
// Backup específico
config_manager_backup_to_nvs("wifi_config");

// Backup de todas as configurações
config_manager_backup_all_to_nvs();
```

### Recuperação

```c
// Recuperar configuração específica
config_manager_restore_from_nvs("wifi_config", true);

// Recuperar todas (útil após reset de fábrica)
config_manager_restore_all_from_nvs(false);
```

## 📁 Estrutura de Arquivos

### Organização Recomendada

```
/spiffs/data/config/
├── ap_config.json          # Configuração Access Point
├── sta_config.json         # Configuração Station
├── mqtt_config.json        # Configuração MQTT
├── network_config.json     # Configuração de rede
├── device_config.json      # Configurações do dispositivo
├── sensor_config.json      # Configurações dos sensores
└── user_config.json        # Configurações do usuário
```

### Migração Automática

A biblioteca detecta automaticamente configurações no formato antigo:

```
/spiffs/wifi_config.json    → /spiffs/data/config/sta_config.json
/spiffs/ap_config.json      → /spiffs/data/config/ap_config.json
/spiffs/mqtt_config.json    → /spiffs/data/config/mqtt_config.json
```

## 🎯 Configurações Pré-definidas

### WiFi Access Point

```c
config_ap_t ap_config = {
    .ssid = "ESP32-Device",
    .password = "12345678",
    .ip = "192.168.4.1",
    .username = "admin",
    .channel = 6,
    .max_connections = 8,
    .hidden = false
};

config_manager_save_ap_config(&ap_config);
```

### WiFi Station

```c
config_sta_t sta_config = {
    .ssid = "MeuWiFi",
    .password = "senhadowifi",
    .enable_static_ip = false,
    .static_ip = "192.168.1.100",
    .subnet = "255.255.255.0",
    .gateway = "192.168.1.1",
    .dns = "8.8.8.8"
};

config_manager_save_sta_config(&sta_config);
```

### Cliente MQTT

```c
config_mqtt_t mqtt_config = {
    .broker_url = "mqtt://192.168.1.100",
    .client_id = "esp32_device_001",
    .username = "mqtt_user",
    .password = "mqtt_pass",
    .port = 1883,
    .qos = 1,
    .retain = false,
    .tls_enabled = false,
    .enabled = true,
    .publish_interval_ms = 10000,
    .keep_alive = 60,
    .clean_session = true
};

config_manager_save_mqtt_config(&mqtt_config);
```

## 🔍 Diagnósticos e Monitoramento

### Informações do Sistema

```c
size_t total_configs, nvs_backups, memory_used;
config_manager_get_system_info(&total_configs, &nvs_backups, &memory_used);

ESP_LOGI("APP", "Configurações: %d", total_configs);
ESP_LOGI("APP", "Backups NVS: %d", nvs_backups);
ESP_LOGI("APP", "Memória: %d bytes", memory_used);
```

### Listar Configurações

```c
char config_list[32][128];
size_t count;

if (config_manager_list_configs(config_list, 32, &count) == CONFIG_MANAGER_OK) {
    for (size_t i = 0; i < count; i++) {
        ESP_LOGI("APP", "Config %d: %s", i + 1, config_list[i]);
    }
}
```

### Relatório de Diagnóstico

```c
char report[2048];
if (config_manager_run_diagnostics(report, sizeof(report)) == CONFIG_MANAGER_OK) {
    ESP_LOGI("APP", "Diagnóstico:\\n%s", report);
}
```

## 🛠️ Configuração Avançada

### Configuração da Biblioteca

```c
config_manager_config_t config = {
    .base_path = "/spiffs",
    .config_dir = "/spiffs/data/config",
    .nvs_namespace = "my_configs",
    .enable_nvs_backup = true,
    .enable_validation = true,
    .enable_legacy_paths = true,
    .auto_create_dirs = true,
    .max_file_size = 16384,              // 16KB máximo por arquivo
    .min_level_read = CONFIG_USER_LEVEL_NONE,
    .min_level_write = CONFIG_USER_LEVEL_BASIC
};

config_manager_init(&config);
```

### Schemas Complexos

```c
// Schema para configuração de sensores
static const double temp_min = -40.0, temp_max = 85.0;
static const char *sensor_types[] = {"temperature", "humidity", "pressure", NULL};

static const config_field_validator_t sensor_fields[] = {
    {"name", CONFIG_TYPE_STRING, true, NULL, NULL, "^[a-zA-Z0-9_]+$", NULL},
    {"type", CONFIG_TYPE_STRING, true, NULL, NULL, NULL, sensor_types},
    {"enabled", CONFIG_TYPE_BOOLEAN, false, NULL, NULL, NULL, NULL},
    {"min_value", CONFIG_TYPE_DOUBLE, false, &temp_min, &temp_max, NULL, NULL},
    {"max_value", CONFIG_TYPE_DOUBLE, false, &temp_min, &temp_max, NULL, NULL},
    {"calibration", CONFIG_TYPE_OBJECT, false, NULL, NULL, NULL, NULL}
};

static const config_schema_t sensor_schema = {
    .config_name = "sensor_config",
    .fields = sensor_fields,
    .field_count = 6,
    .required_level = CONFIG_USER_LEVEL_ADMIN
};
```

## 📚 API Completa

### Gerenciamento Principal

- `config_manager_init()` - Inicializar biblioteca
- `config_manager_deinit()` - Desinicializar
- `config_manager_is_initialized()` - Verificar se inicializada

### Operações com JSON

- `config_manager_save_json()` - Salvar objeto JSON
- `config_manager_load_json()` - Carregar objeto JSON
- `config_manager_save_json_string()` - Salvar string JSON
- `config_manager_load_json_string()` - Carregar string JSON
- `config_manager_exists()` - Verificar se existe
- `config_manager_delete()` - Excluir configuração

### Validação

- `config_manager_register_schema()` - Registrar schema
- `config_manager_validate_json()` - Validar dados
- `config_manager_get_schema()` - Obter schema registrado

### Backup e NVS

- `config_manager_backup_to_nvs()` - Backup específico
- `config_manager_backup_all_to_nvs()` - Backup completo
- `config_manager_restore_from_nvs()` - Restaurar específico
- `config_manager_restore_all_from_nvs()` - Restaurar tudo
- `config_manager_clear_nvs_backup()` - Limpar backup

### Usuários e Acesso

- `config_manager_set_user_level()` - Definir nível
- `config_manager_get_user_level()` - Obter nível
- `config_manager_check_access()` - Verificar permissão
- `config_manager_save_login_state()` - Salvar login
- `config_manager_load_login_state()` - Carregar login

### Callbacks e Processadores

- `config_manager_register_processor()` - Registrar processador
- `config_manager_register_change_callback()` - Registrar callback

### Configurações Pré-definidas

- `config_manager_save_ap_config()` / `config_manager_load_ap_config()`
- `config_manager_save_sta_config()` / `config_manager_load_sta_config()`
- `config_manager_save_mqtt_config()` / `config_manager_load_mqtt_config()`
- `config_manager_save_network_config()` / `config_manager_load_network_config()`

### Utilitários

- `config_manager_get_error_string()` - Converter erro para string
- `config_manager_get_system_info()` - Informações do sistema
- `config_manager_list_configs()` - Listar configurações
- `config_manager_run_diagnostics()` - Executar diagnósticos

## 🔧 Integração com Outras Bibliotecas

Esta biblioteca se integra perfeitamente com:

- **AP Manager** - Configuração automática de AP
- **SPIFFS File Manager** - Servir configs via web
- **WiFi Manager** - Aplicar configurações WiFi
- **MQTT Client** - Usar configs MQTT automaticamente

```c
// Exemplo de integração
config_ap_t ap_config;
if (config_manager_load_ap_config(&ap_config) == CONFIG_MANAGER_OK) {
    // Usar com AP Manager
    ap_manager_start_ap(&ap_config);
}
```

## 📊 Performance

- **Memória Base**: ~12KB RAM
- **Arquivo Máximo**: 8KB por configuração (configurável)
- **Configurações Simultâneas**: 32 (configurável)
- **Schemas de Validação**: 32 schemas personalizados
- **Callbacks**: 8 callbacks por configuração
- **Backup NVS**: Automático e transparente

## 🧪 Testes e Validação

```c
// Executar suite de testes
void run_config_tests(void) {
    // Teste de salvamento/carregamento
    assert(test_save_load_cycle());
    
    // Teste de validação
    assert(test_validation_system());
    
    // Teste de backup/recuperação
    assert(test_nvs_backup());
    
    // Teste de controle de acesso
    assert(test_access_control());
    
    ESP_LOGI("TEST", "Todos os testes passaram!");
}
```

## 📄 Licença

MIT License - Livre para uso comercial e pessoal.

## 🆘 Suporte

Para dúvidas, bugs ou sugestões:

1. Verifique os exemplos incluídos
2. Consulte a documentação da API
3. Execute os diagnósticos: `config_manager_run_diagnostics()`
4. Verifique os logs do ESP-IDF
5. Abra uma issue no repositório

---

**Desenvolvido para ESP32 com ❤️ - Gerencie suas configurações de forma profissional!**