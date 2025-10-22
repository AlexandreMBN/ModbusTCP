# Gerenciador WiFi - ESP32

## Visão Geral

Este módulo implementa um gerenciador WiFi robusto para ESP32 que suporta:
- Modo Access Point (AP)
- Modo Station (STA)
- Modo AP+STA simultâneo
- Scan de redes WiFi
- Reconexão automática
- Gestão de status e configurações

## Melhorias Implementadas

### 1. **Gestão de Recursos Melhorada**
- ✅ Mutex para sincronização de operações WiFi
- ✅ Verificação de erros em todas as operações críticas
- ✅ Função de limpeza (`wifi_cleanup()`) para liberar recursos
- ✅ Gestão adequada de interfaces de rede

### 2. **Estrutura de Status WiFi**
```c
typedef struct {
    bool is_connected;
    bool ap_active;
    char current_ssid[WIFI_SSID_MAX_LEN];
    char ip_address[16];
    int rssi;
    char status_message[WIFI_STATUS_MSG_MAX_LEN];
} wifi_status_t;
```

### 3. **Tratamento de Erros Robusto**
- Verificação de parâmetros de entrada
- Logs detalhados de erros
- Timeouts configuráveis
- Fallback automático para modo AP

### 4. **Configurações Centralizadas**
- Constantes definidas em `config.h`
- Estruturas organizadas para configurações
- Validação de parâmetros

## API Principal

### Inicialização
```c
void start_wifi_ap();  // Inicia modo AP
```

### Conexão WiFi
```c
void wifi_connect(const char* ssid, const char* password);
```

### Scan de Redes
```c
void wifi_scan();  // Escaneia redes disponíveis
```

### Gestão de Status
```c
wifi_status_t wifi_get_status();           // Obtém status atual
void wifi_set_status_message(const char* message);
bool wifi_is_initialized();                // Verifica se WiFi está inicializado
```

### Limpeza
```c
void wifi_disconnect();  // Desconecta STA
void wifi_cleanup();     // Limpa todos os recursos
```

### Configuração
```c
esp_err_t wifi_set_ap_config(const char* ssid, const char* password, uint8_t channel);
esp_err_t wifi_set_sta_config(const char* ssid, const char* password);
```

## Constantes de Configuração

```c
#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASS_MAX_LEN 64
#define WIFI_STATUS_MSG_MAX_LEN 256
#define MAX_APs 20

// Timeouts
#define WIFI_CONNECT_TIMEOUT_MS 30000
#define WIFI_SCAN_TIMEOUT_MS 10000
#define WIFI_FALLBACK_TIMEOUT_MS 60000
```

## Fluxo de Operação

1. **Inicialização**: `start_wifi_ap()` inicia o modo AP
2. **Scan**: `wifi_scan()` encontra redes disponíveis
3. **Conexão**: `wifi_connect()` tenta conectar em uma rede
4. **Fallback**: Se a conexão falhar, mantém AP ativo
5. **Monitoramento**: Status é atualizado continuamente

## Tratamento de Erros

### Códigos de Erro Comuns
- `ESP_ERR_INVALID_ARG`: Parâmetros inválidos
- `ESP_ERR_INVALID_STATE`: WiFi não inicializado
- `ESP_ERR_TIMEOUT`: Timeout em operação
- `ESP_OK`: Operação bem-sucedida

### Logs de Debug
O módulo usa ESP_LOG para diferentes níveis:
- `ESP_LOGE`: Erros críticos
- `ESP_LOGW`: Avisos
- `ESP_LOGI`: Informações gerais
- `ESP_LOGD`: Debug (quando habilitado)

## Exemplo de Uso

```c
#include "wifi_manager.h"

void app_main() {
    // Inicia AP
    start_wifi_ap();
    
    // Escaneia redes
    wifi_scan();
    
    // Tenta conectar
    wifi_connect("MinhaRede", "MinhaSenha");
    
    // Verifica status
    wifi_status_t status = wifi_get_status();
    if (status.is_connected) {
        printf("Conectado em %s com IP %s\n", 
               status.current_ssid, status.ip_address);
    }
}
```

## Considerações de Segurança

1. **Senhas**: Sempre use senhas com pelo menos 8 caracteres
2. **AP Aberto**: Evite usar AP sem senha em produção
3. **Logs**: Não logue senhas ou informações sensíveis
4. **Timeout**: Configure timeouts apropriados para seu ambiente

## Troubleshooting

### Problemas Comuns

1. **WiFi não inicializa**
   - Verifique se `esp_netif_init()` foi chamado
   - Confirme se há memória suficiente

2. **Conexão falha**
   - Verifique SSID e senha
   - Confirme se a rede está no alcance
   - Verifique logs para códigos de erro

3. **AP não aparece**
   - Verifique se o modo AP está ativo
   - Confirme configurações de canal
   - Verifique se não há conflitos de frequência

### Debug
Para habilitar logs detalhados, configure:
```c
esp_log_level_set("WIFI_MANAGER", ESP_LOG_DEBUG);
```

## Próximas Melhorias Sugeridas

1. **Persistência de Configurações**: Salvar configurações em NVS
2. **Reconexão Automática**: Implementar reconexão em caso de perda
3. **WPS**: Suporte a WPS para configuração fácil
4. **Múltiplas Redes**: Suporte a múltiplas redes com prioridade
5. **Monitoramento de Qualidade**: RSSI e qualidade de sinal
6. **Configuração via Web**: Interface web para configuração 