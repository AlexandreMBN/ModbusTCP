# Sistema de Configuração Modular

## Visão Geral

O sistema foi reestruturado para usar arquivos JSON separados para cada módulo, substituindo a antiga abordagem mista de NVS + config.json único.

## Estrutura de Arquivos

### 1. `/spiffs/rtu_config.json` - Configurações Modbus RTU
```json
{
  "uart_port": 2,
  "baud_rate": 9600,
  "data_bits": 8,
  "parity": 0,
  "stop_bits": 1,
  "slave_address": 1,
  "register_ranges": {
    "input_registers": [1000, 2000],
    "holding_registers": [4000, 6000, 9000]
  }
}
```

### 2. `/spiffs/ap_config.json` - Configurações WiFi Access Point
```json
{
  "ssid": "ESP32_WebServer",
  "password": "12345678",
  "max_connections": 4,
  "channel": 1,
  "ssid_hidden": false,
  "authmode": 4
}
```

### 3. `/spiffs/sta_config.json` - Configurações WiFi Station
```json
{
  "ssid": "MeuWiFi",
  "password": "minha_senha",
  "dhcp_enabled": true,
  "static_ip": "192.168.1.100",
  "gateway": "192.168.1.1",
  "subnet": "255.255.255.0"
}
```

### 4. `/spiffs/mqtt_config.json` - Configurações Cliente MQTT
```json
{
  "enabled": true,
  "broker_uri": "mqtt://broker.example.com:1883",
  "client_id": "ESP32_Lambda_Sensor",
  "username": "user",
  "password": "pass",
  "keep_alive": 60,
  "clean_session": true,
  "qos": 1,
  "retain": false,
  "topics": {
    "lambda": "/sensors/lambda",
    "status": "/device/status"
  },
  "tls": {
    "enabled": false,
    "cert": "",
    "key": "",
    "ca": ""
  }
}
```

### 5. `/spiffs/network_config.json` - Configurações de Rede
```json
{
  "hostname": "esp32-webserver",
  "dhcp_enabled": true,
  "static_ip": "192.168.1.100",
  "gateway": "192.168.1.1",
  "subnet": "255.255.255.0",
  "dns1": "8.8.8.8",
  "dns2": "8.8.4.4"
}
```

## APIs Disponíveis

### Funções de Carregamento
- `esp_err_t load_rtu_config(rtu_config_t *config)`
- `esp_err_t load_ap_config(ap_config_t *config)`
- `esp_err_t load_sta_config(sta_config_t *config)`
- `esp_err_t load_mqtt_config(mqtt_config_t *config)`
- `esp_err_t load_network_config(network_config_t *config)`

### Funções de Salvamento
- `esp_err_t save_rtu_config(const rtu_config_t *config)`
- `esp_err_t save_ap_config(const ap_config_t *config)`
- `esp_err_t save_sta_config(const sta_config_t *config)`
- `esp_err_t save_mqtt_config(const mqtt_config_t *config)`
- `esp_err_t save_network_config(const network_config_t *config)`

## Vantagens da Nova Estrutura

### 1. **Persistência das Configurações MQTT**
- As configurações MQTT agora são salvas em arquivo JSON
- Sobrevivem a reinicializações do dispositivo
- Não são perdidas como antes (quando ficavam apenas na RAM)

### 2. **Modularidade**
- Cada módulo tem seu próprio arquivo de configuração
- Facilita manutenção e debugging
- Permite configurações independentes

### 3. **Flexibilidade**
- Estruturas tipadas para cada configuração
- Validação de dados por módulo
- Fácil extensão com novos parâmetros

### 4. **Backup e Restauração**
- Arquivos individuais podem ser backup/restaurados
- Configurações podem ser copiadas entre dispositivos
- Versionamento de configurações mais fácil

## Migração dos Dados

### O que permanece no NVS:
- **Login states**: Estados de login dos usuários
- **User levels**: Níveis de acesso dos usuários
- **Security tokens**: Tokens de segurança
- **Device credentials**: Credenciais sensíveis do dispositivo

### O que migrou para arquivos JSON:
- **Configurações Modbus RTU**: Parâmetros de comunicação serial
- **Configurações WiFi**: Access Point e Station
- **Configurações MQTT**: Broker, tópicos, credenciais
- **Configurações de Rede**: IP, gateway, DNS

## Comportamento de Inicialização

1. **Primeira execução**: Arquivos JSON são criados com valores padrão
2. **Execuções subsequentes**: Configurações são carregadas dos arquivos
3. **Arquivo corrompido**: Sistema usa valores padrão e recria o arquivo
4. **Arquivo ausente**: Sistema cria novo arquivo com defaults

## Exemplo de Uso

```c
#include "config_manager.h"

void example_usage() {
    // Carregar configuração MQTT
    mqtt_config_t mqtt_config;
    if (load_mqtt_config(&mqtt_config) == ESP_OK) {
        printf("Broker: %s\n", mqtt_config.broker_uri);
    }
    
    // Modificar e salvar
    strcpy(mqtt_config.broker_uri, "mqtt://novo.broker.com:1883");
    if (save_mqtt_config(&mqtt_config) == ESP_OK) {
        printf("Configuração MQTT salva com sucesso!\n");
    }
}
```

## Logs de Debug

O sistema inclui logs detalhados para facilitar debugging:
- Carregamento de configurações
- Salvamento de arquivos
- Erros de parsing JSON
- Valores padrão aplicados

## Tratamento de Erros

- **ESP_OK**: Operação bem-sucedida
- **ESP_FAIL**: Erro geral (arquivo não encontrado, JSON inválido, etc.)
- **ESP_ERR_NO_MEM**: Memória insuficiente
- **ESP_ERR_INVALID_ARG**: Parâmetros inválidos

## Considerações de Performance

- Configurações são carregadas apenas quando necessário
- Cache em memória evita múltiplas leituras do arquivo
- Operações de I/O são minimizadas
- Estruturas compactas reduzem uso de memória

## Manutenção

Para adicionar novos parâmetros:
1. Atualizar a estrutura correspondente em `config_manager.h`
2. Modificar as funções de load/save para incluir o novo campo
3. Atualizar valores padrão se necessário
4. Testar compatibilidade com versões anteriores