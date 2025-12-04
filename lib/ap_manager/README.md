# 📡 AP Manager Library for ESP32

Uma biblioteca completa e robusta para gerenciamento de Access Point WiFi em projetos ESP32 com ESP-IDF.

## ✨ Características Principais

- 🔧 **Configuração dinâmica**: SSID, password, IP, canal e conexões máximas configuráveis
- 💾 **Persistência dupla**: Salva configurações em SPIFFS (prioridade) e NVS (backup)
- 🔍 **Scan de redes**: Busca automática por redes WiFi disponíveis  
- 🌐 **Modo híbrido**: Suporte AP+STA com troca automática para STA-only
- 🔒 **Thread-safe**: Operações seguras com semáforos e mutexes
- 📡 **Eventos personalizados**: Sistema de callbacks para monitoramento
- 🛠️ **Fácil integração**: Interface simples e bem documentada
- 🔄 **Auto-reconexão**: Reconexão automática a redes WiFi salvas
- 📊 **Status detalhado**: Informações completas sobre estado da conexão

## 🚀 Instalação

### ⭐ Opção 1: PlatformIO (Recomendado)

1. Copie a pasta `ap_manager` para o diretório `lib/` do seu projeto:
```bash
cp -r ap_manager /caminho/para/seu/projeto/lib/
```

2. A biblioteca será automaticamente detectada pelo PlatformIO
3. Inclua no seu código: `#include <ap_manager.h>`

**Estrutura do projeto:**
```
meu_projeto/
├── src/
│   └── main.c
├── lib/
│   └── ap_manager/          # ← Biblioteca aqui
└── platformio.ini
```

### Opção 2: Como Componente ESP-IDF

1. Copie a pasta `ap_manager` para o diretório `components/` do seu projeto:
```bash
cp -r ap_manager /caminho/para/seu/projeto/components/
```

2. Configure seu `CMakeLists.txt` principal:
```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(meu_projeto)
```

### Opção 3: Como Submódulo Git

1. Adicione como submódulo:
```bash
git submodule add <repo_url> lib/ap_manager
```

2. Inicialize o submódulo:
```bash
git submodule update --init --recursive
```

## 📋 Dependências

A biblioteca requer os seguintes componentes ESP-IDF:

- `esp_wifi` - Funcionalidades WiFi básicas
- `esp_netif` - Interface de rede
- `esp_event` - Sistema de eventos  
- `esp_system` - Funcionalidades do sistema
- `nvs_flash` - Armazenamento não-volátil
- `spiffs` - Sistema de arquivos SPIFFS
- `json` - Parsing JSON (cJSON)

## 🔧 Uso Básico

### 1. Inicialização Simples (PlatformIO)

```c
#include <ap_manager.h>  // Inclusão automática no PlatformIO

void app_main(void) {
    // Inicializar NVS
    nvs_flash_init();
    
    // Inicializar AP Manager
    esp_err_t ret = ap_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE("APP", "Falha ao inicializar AP Manager");
        return;
    }
    
    // Iniciar Access Point
    ret = ap_manager_start_ap();
    if (ret == ESP_OK) {
        ESP_LOGI("APP", "Access Point iniciado com sucesso!");
    }
}
```

### 2. Configuração Personalizada do AP

```c
// Configurar Access Point personalizado
ap_manager_config_t ap_config = {
    .ssid = \"MeuESP32\",
    .password = \"minhasenha123\",
    .ip = \"192.168.10.1\", 
    .channel = 6,
    .max_connections = 4,
    .hidden = false
};

// Salvar configuração
esp_err_t ret = ap_manager_set_ap_config(&ap_config);
```

### 3. Conexão a Rede WiFi (Modo Station)

```c
// Conectar a uma rede WiFi
esp_err_t ret = ap_manager_connect_sta(\"MinhaRedeWiFi\", \"minhaSenha\");
if (ret == ESP_OK) {
    ESP_LOGI(\"APP\", \"Tentando conectar...\");
}
```

### 4. Scan de Redes WiFi

```c
// Iniciar scan
ap_manager_start_scan();

// Aguardar e obter resultados
wifi_ap_record_t networks[20];
uint16_t count;
esp_err_t ret = ap_manager_get_scan_results(networks, 20, &count);

if (ret == ESP_OK) {
    for (int i = 0; i < count; i++) {
        ESP_LOGI(\"SCAN\", \"Rede %d: %s (RSSI: %d)\", 
                i, networks[i].ssid, networks[i].rssi);
    }
}
```

### 5. Monitoramento com Callbacks

```c
// Callback para eventos
void meu_callback(ap_manager_event_id_t event, void *data) {
    switch (event) {
        case AP_MANAGER_EVENT_AP_STARTED:
            ESP_LOGI(\"EVENT\", \"AP iniciado!\");
            break;
        case AP_MANAGER_EVENT_STA_GOT_IP:
            ESP_LOGI(\"EVENT\", \"IP obtido: %s\", 
                   ((ap_manager_status_t*)data)->ip_address);
            break;
        // ... outros eventos
    }
}

// Registrar callback
ap_manager_set_event_callback(meu_callback);
```

## 📊 Monitoramento de Status

```c
// Obter status atual
ap_manager_status_t status = ap_manager_get_status();

ESP_LOGI(\"STATUS\", \"AP Ativo: %s\", status.ap_active ? \"Sim\" : \"Não\");
ESP_LOGI(\"STATUS\", \"Conectado: %s\", status.is_connected ? \"Sim\" : \"Não\");
ESP_LOGI(\"STATUS\", \"IP: %s\", status.ip_address);
ESP_LOGI(\"STATUS\", \"SSID: %s\", status.current_ssid);
ESP_LOGI(\"STATUS\", \"RSSI: %d dBm\", status.rssi);
```

## ⚙️ Funcionalidades Avançadas

### Auto-Switch para Modo Station-Only

```c
// Trocar automaticamente para STA-only após 60 segundos de conexão
ap_manager_auto_switch_to_sta(60000);
```

### Configuração de IP Estático

```c
// Configurar IP estático para modo Station
ap_manager_set_static_ip(\"192.168.1.100\", \"255.255.255.0\", 
                        \"192.168.1.1\", \"8.8.8.8\");
```

### Importação/Exportação de Configurações

```c
// Exportar configurações para JSON
char *json_config = NULL;
esp_err_t ret = ap_manager_config_export_json(&json_config);
if (ret == ESP_OK) {
    ESP_LOGI(\"CONFIG\", \"Configurações: %s\", json_config);
    free(json_config);
}

// Importar configurações de JSON
const char *config_json = \"{\\\"ap_config\\\":{...}}\";
ap_manager_config_import_json(config_json);
```

## 🎯 Eventos Disponíveis

| Evento | Descrição |
|--------|-----------|
| `AP_MANAGER_EVENT_AP_STARTED` | Access Point iniciado |
| `AP_MANAGER_EVENT_AP_STOPPED` | Access Point parado |
| `AP_MANAGER_EVENT_STA_CONNECTED` | Conectado a rede WiFi |
| `AP_MANAGER_EVENT_STA_DISCONNECTED` | Desconectado da rede WiFi |
| `AP_MANAGER_EVENT_STA_GOT_IP` | IP obtido em modo Station |
| `AP_MANAGER_EVENT_SCAN_COMPLETED` | Scan de redes concluído |
| `AP_MANAGER_EVENT_CONFIG_SAVED` | Configuração salva |
| `AP_MANAGER_EVENT_CONFIG_LOADED` | Configuração carregada |

## 📂 Estrutura de Arquivos

```
ap_manager/                    # ← Biblioteca na pasta lib/
├── include/
│   ├── ap_manager.h           # Header principal
│   └── ap_manager_config.h    # Configuração persistente
├── src/
│   ├── ap_manager.c           # Implementação principal  
│   └── ap_manager_config.c    # Gerenciamento de configuração
├── examples/
│   ├── basic_example.c        # Exemplo de uso básico
│   └── platformio_example.c   # Exemplo específico PlatformIO
├── library.json              # Configuração PlatformIO
├── CMakeLists.txt            # Configuração ESP-IDF
├── component.mk              # Configuração Make (IDF legado)
├── Kconfig                   # Opções de configuração
└── README.md                 # Esta documentação
```

## ⚙️ Configurações (menuconfig)

Execute `idf.py menuconfig` e vá para **Component config > AP Manager Configuration**:

- **Maximum number of APs**: Número máximo de APs no scan (padrão: 20)
- **Default AP SSID**: SSID padrão (padrão: \"ESP32_CONFIG\")  
- **Default AP Password**: Senha padrão (padrão: \"12345678\")
- **Default AP IP**: IP padrão (padrão: \"192.168.4.1\")
- **Auto-save configuration**: Salvamento automático (padrão: habilitado)
- **Enable automatic switch**: Auto-switch STA-only (padrão: habilitado)

## 🔧 API Completa

### Inicialização e Finalização

```c
esp_err_t ap_manager_init(void);
esp_err_t ap_manager_deinit(void);
bool ap_manager_is_initialized(void);
```

### Gerenciamento do Access Point

```c
esp_err_t ap_manager_start_ap(void);
esp_err_t ap_manager_stop_ap(void);
esp_err_t ap_manager_set_ap_config(const ap_manager_config_t *config);
esp_err_t ap_manager_get_ap_config(ap_manager_config_t *config);
```

### Conexão Station

```c
esp_err_t ap_manager_connect_sta(const char *ssid, const char *password);
esp_err_t ap_manager_disconnect_sta(void);
esp_err_t ap_manager_set_sta_config(const ap_manager_sta_config_t *config);
esp_err_t ap_manager_get_sta_config(ap_manager_sta_config_t *config);
```

### Scan de Redes

```c
esp_err_t ap_manager_start_scan(void);
esp_err_t ap_manager_get_scan_results(wifi_ap_record_t *records, 
                                     uint16_t max_records, uint16_t *count);
bool ap_manager_is_scan_in_progress(void);
```

### Monitoramento e Status

```c
ap_manager_status_t ap_manager_get_status(void);
void ap_manager_set_status_message(const char *message);
esp_err_t ap_manager_set_event_callback(ap_manager_event_cb_t callback);
esp_err_t ap_manager_unset_event_callback(void);
```

### Configuração Avançada

```c
esp_err_t ap_manager_set_static_ip(const char *ip, const char *netmask,
                                  const char *gateway, const char *dns);
esp_err_t ap_manager_enable_dhcp(void);
esp_err_t ap_manager_auto_switch_to_sta(uint32_t timeout_ms);
esp_err_t ap_manager_set_wifi_mode(wifi_mode_t mode);
```

## 💾 Armazenamento de Configuração

A biblioteca utiliza um sistema duplo de armazenamento:

### SPIFFS (Prioridade)
```
/spiffs/data/config/
├── ap_config.json     # Configurações do Access Point
└── sta_config.json    # Configurações Station (WiFi conectado)
```

### NVS (Backup)
```
Namespace: \"ap_manager\"
├── ap_config          # Backup configuração AP
└── sta_config         # Backup configuração Station
```

## 🚨 Tratamento de Erros

```c
esp_err_t ret = ap_manager_start_ap();
switch (ret) {
    case ESP_OK:
        ESP_LOGI(\"APP\", \"AP iniciado com sucesso\");
        break;
    case ESP_ERR_INVALID_STATE:
        ESP_LOGE(\"APP\", \"AP Manager não inicializado\");
        break;
    case ESP_ERR_WIFI_NOT_INIT:
        ESP_LOGE(\"APP\", \"WiFi não inicializado\");
        break;
    default:
        ESP_LOGE(\"APP\", \"Erro: %s\", esp_err_to_name(ret));
        break;
}
```

## 🔒 Thread Safety

Todas as operações da biblioteca são thread-safe, utilizando mutexes internos para:

- ✅ Operações WiFi (conexão, desconexão, scan)
- ✅ Acesso ao status global
- ✅ Resultados de scan
- ✅ Salvamento/carregamento de configurações

## 📝 Exemplo Completo

Veja o arquivo `examples/basic_example.c` para um exemplo completo que demonstra:

- Inicialização do AP Manager
- Configuração personalizada do AP
- Scan automático de redes
- Conexão a WiFi salvo
- Monitoramento de status
- Callbacks de eventos

## 🐛 Troubleshooting

### Problema: AP não inicia
**Solução**: Verifique se o NVS foi inicializado e se há memória suficiente

### Problema: Configuração não salva
**Solução**: Verifique se SPIFFS foi montado corretamente e há espaço disponível

### Problema: Scan não funciona
**Solução**: Certifique-se de que o WiFi foi inicializado e não há outro scan em progresso

### Problema: Conexão WiFi falha
**Solução**: Verifique credenciais e se a rede está no alcance

## 📞 Suporte

Para problemas, sugestões ou contribuições:

1. Verifique os logs de debug (nível INFO ou superior)
2. Consulte a documentação da API
3. Teste com o exemplo básico primeiro
4. Verifique se todas as dependências estão instaladas

## 🏷️ Versão

**Versão**: 1.0.0  
**Compatibilidade**: ESP-IDF v4.4+  
**Testado com**: ESP32, ESP32-S2, ESP32-S3, ESP32-C3

## 📄 Licença

Esta biblioteca foi extraída e modularizada do projeto WebServerCompleto e mantém a mesma licença do projeto original.

---

**🎉 Agora você tem uma biblioteca completa e reutilizável para gerenciamento de Access Point em seus projetos ESP32!**