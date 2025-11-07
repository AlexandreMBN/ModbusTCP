# Nova Estrutura de Configuração Modular

## 📁 Arquivos de Configuração SPIFFS

A nova estrutura organiza as configurações em arquivos JSON específicos para cada módulo:

### `/spiffs/rtu_config.json` - Configurações Modbus RTU
```json
{
  "reg1000": {
    "baudrate": 115200,
    "endereco": 200,
    "paridade": 0
  },
  "reg2000": {
    "dataValue": 0
  },
  "reg4000": {
    "lambdaValue": 0,
    "lambdaRef": 1000,
    "heatValue": 0,
    "heatRef": 800,
    "output_mb": 0,
    "PROBE_DEMAGED": 0,
    "PROBE_TEMP_OUT_OF_RANGE": 0,
    "COMPRESSOR_FAIL": 0
  },
  "reg6000": {
    "maxDac0": 4095,
    "forcaValorDAC": 0,
    "nada": 0,
    "dACGain0": 1000,
    "dACOffset0": 0
  },
  "reg9000": {
    "valorZero": 0,
    "valorUm": 1,
    "firmVerHi": 1,
    "firmVerLo": 0,
    "valorQuatro": 4,
    "valorCinco": 5,
    "lotnum0": 0,
    "lotnum1": 0,
    "lotnum2": 0,
    "lotnum3": 0,
    "lotnum4": 0,
    "lotnum5": 0,
    "wafnum": 0,
    "coordx0": 0,
    "coordx1": 0,
    "valor17": 17,
    "valor18": 18,
    "valor19": 19
  }
}
```

### `/spiffs/ap_config.json` - Configurações Access Point
```json
{
  "ssid": "ESP32-WebServer",
  "username": "admin",
  "password": "12345678",
  "ip": "192.168.4.1"
}
```

### `/spiffs/sta_config.json` - Configurações WiFi Station
```json
{
  "ssid": "MinhaRedeWiFi",
  "password": "minhasenha"
}
```

### `/spiffs/mqtt_config.json` - Configurações Cliente MQTT
```json
{
  "broker_url": "mqtt://broker.hivemq.com",
  "client_id": "ESP32_SondaLambda",
  "username": "",
  "password": "",
  "port": 1883,
  "qos": 1,
  "retain": false,
  "tls_enabled": false,
  "ca_path": "/spiffs/isrgrootx1.pem",
  "enabled": true,
  "publish_interval_ms": 1000
}
```

### `/spiffs/network_config.json` - Configurações de Rede
```json
{
  "ip": "192.168.1.100",
  "mask": "255.255.255.0",
  "gateway": "192.168.1.1",
  "dns": "8.8.8.8"
}
```

## 🔧 API Functions

### RTU Config
```c
esp_err_t save_rtu_config(void);
esp_err_t load_rtu_config(void);
```

### AP Config
```c
typedef struct {
    char ssid[32];
    char username[32];
    char password[64];
    char ip[16];
} ap_config_t;

esp_err_t save_ap_config(const ap_config_t* config);
esp_err_t load_ap_config(ap_config_t* config);
```

### STA Config
```c
typedef struct {
    char ssid[32];
    char password[64];
} sta_config_t;

esp_err_t save_sta_config(const sta_config_t* config);
esp_err_t load_sta_config(sta_config_t* config);
```

### MQTT Config
```c
typedef struct {
    char broker_url[128];
    char client_id[32];
    char username[32];
    char password[64];
    uint16_t port;
    uint8_t qos;
    bool retain;
    bool tls_enabled;
    char ca_path[128];
    bool enabled;
    uint32_t publish_interval_ms;
} mqtt_config_t;

esp_err_t save_mqtt_config(const mqtt_config_t* config);
esp_err_t load_mqtt_config(mqtt_config_t* config);
```

### Network Config
```c
typedef struct {
    char ip[16];
    char mask[16];
    char gateway[16];
    char dns[16];
} network_config_t;

esp_err_t save_network_config(const network_config_t* config);
esp_err_t load_network_config(network_config_t* config);
```

## 🔒 NVS (Mantido por Segurança)

Algumas configurações sensíveis permanecem no NVS:
- Estados de login (login_state, login_state_root)
- Níveis de usuário (user_level)

## 🔄 Funções de Compatibilidade

Para migração gradual, as funções antigas ainda funcionam:
```c
// Legacy - redireciona para save_rtu_config()
esp_err_t save_config(void);
esp_err_t load_config(void);

// Legacy - redireciona para save/load_sta_config()
void save_wifi_config(const char* ssid, const char* password);
void read_wifi_config(char* ssid, size_t ssid_len, char* password, size_t password_len);
```

## ✅ Vantagens da Nova Estrutura

1. **Modularidade**: Cada configuração em seu próprio arquivo
2. **Manutenibilidade**: Fácil identificação e edição de configurações
3. **Escalabilidade**: Adição de novos módulos sem impacto em outros
4. **Debugging**: Logs específicos por módulo
5. **Backup**: Backup seletivo de configurações específicas
6. **Compatibilidade**: Funções legacy mantidas para transição suave

## 🔄 Migration Guide

Para migrar código existente:

1. **Substitua chamadas antigas**:
   ```c
   // Antigo
   save_config();
   
   // Novo
   save_rtu_config();
   ```

2. **Use estruturas tipadas**:
   ```c
   // Antigo
   save_wifi_config("ssid", "password");
   
   // Novo
   sta_config_t config = {"ssid", "password"};
   save_sta_config(&config);
   ```

3. **Configure MQTT persistente**:
   ```c
   mqtt_config_t mqtt_cfg;
   load_mqtt_config(&mqtt_cfg);  // Carrega do arquivo
   mqtt_set_config(&mqtt_cfg);   // Aplica no cliente
   ```