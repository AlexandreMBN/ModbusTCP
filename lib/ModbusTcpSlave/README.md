# ModbusTcpSlave

Uma biblioteca completa para implementar **Modbus TCP Slave** em projetos ESP32 usando PlatformIO e ESP-IDF.

## 🎯 **Características**

- ✅ **API Simples** - Interface fácil de usar
- ✅ **Thread-Safe** - Seguro para uso em múltiplas tasks
- ✅ **Callbacks** - Notificações de eventos personalizáveis
- ✅ **Modular** - Pode ser integrada facilmente em projetos existentes
- ✅ **Completa** - Suporte a todos os tipos de registros Modbus
- ✅ **Configurável** - Portas, timeouts e conexões personalizáveis

## 📋 **Registros Suportados**

| Tipo | Quantidade | Endereço | Acesso | Tipo de Dados |
|------|------------|----------|---------|---------------|
| **Holding Registers** | 8 | 0-7 | Leitura/Escrita | Float (32-bit) |
| **Input Registers** | 8 | 0-7 | Somente Leitura | Float (32-bit) |
| **Coils** | 16 | 0-15 | Leitura/Escrita | Boolean |
| **Discrete Inputs** | 8 | 0-7 | Somente Leitura | Boolean |

## 🚀 **Instalação**

### Método 1: Cópia Direta
```bash
# Copie a pasta ModbusTcpSlave para o diretório lib/ do seu projeto
cp -r ModbusTcpSlave /caminho/para/seu/projeto/lib/
```

### Método 2: PlatformIO Library
```ini
; platformio.ini
[env:esp32dev]
lib_extra_dirs = lib
lib_deps = 
    ModbusTcpSlave@file://lib/ModbusTcpSlave
```

## 📖 **Uso Básico**

### 1. **Inicialização Simples**

```c
#include "modbus_tcp_slave.h"

static modbus_tcp_handle_t mb_handle;

void init_modbus() {
    // Configuração básica
    modbus_tcp_config_t config = {
        .port = 502,
        .slave_id = 1,
        .netif = get_sta_netif(),  // Sua interface de rede
        .auto_start = true
    };
    
    // Inicializar
    ESP_ERROR_CHECK(modbus_tcp_slave_init(&config, &mb_handle));
    
    ESP_LOGI("APP", "Modbus TCP Slave iniciado na porta 502");
}
```

### 2. **Com Callbacks Personalizados**

```c
#include "modbus_tcp_slave.h"

// Callbacks
void on_register_write(uint16_t addr, modbus_reg_type_t reg_type, uint32_t value) {
    ESP_LOGI("MODBUS", "Registro escrito - Tipo: %d, Addr: %d, Valor: %lu", 
             reg_type, addr, value);
}

void on_connection_change(bool connected, uint8_t count) {
    ESP_LOGI("MODBUS", "Conexão: %s, Total: %d", 
             connected ? "Conectado" : "Desconectado", count);
}

void init_modbus_with_callbacks() {
    modbus_tcp_config_t config = {
        .port = 502,
        .slave_id = 1,
        .netif = get_sta_netif(),
        .auto_start = false  // Iniciar manualmente
    };
    
    modbus_tcp_callbacks_t callbacks = {
        .on_register_write = on_register_write,
        .on_connection_change = on_connection_change
    };
    
    // Inicializar e configurar callbacks
    ESP_ERROR_CHECK(modbus_tcp_slave_init(&config, &mb_handle));
    ESP_ERROR_CHECK(modbus_tcp_register_callbacks(mb_handle, &callbacks));
    ESP_ERROR_CHECK(modbus_tcp_slave_start(mb_handle));
}
```

### 3. **Gerenciamento de Registros**

```c
// Atualizar dados dos sensores
void update_sensor_data() {
    float temp = read_temperature();
    float humidity = read_humidity();
    bool alarm = check_alarm();
    
    // Atualizar Input Registers (somente leitura)
    modbus_tcp_set_input_reg_float(mb_handle, 0, temp);
    modbus_tcp_set_input_reg_float(mb_handle, 1, humidity);
    
    // Atualizar Discrete Input
    modbus_tcp_set_discrete_input(mb_handle, 0, alarm);
}

// Ler configurações do sistema
void read_system_config() {
    float setpoint;
    bool enable;
    
    // Ler Holding Register (leitura/escrita)
    modbus_tcp_get_holding_reg_float(mb_handle, 0, &setpoint);
    
    // Ler Coil
    modbus_tcp_get_coil(mb_handle, 0, &enable);
    
    // Aplicar configurações
    set_system_setpoint(setpoint);
    set_system_enable(enable);
}
```

## 🏗️ **Integração em Projeto Modular**

### Estrutura Recomendada

```
seu_projeto/
├── lib/
│   └── ModbusTcpSlave/          <- Esta biblioteca
├── src/
│   ├── main.c
│   ├── wifi_module.c            <- Seu módulo WiFi
│   ├── modbus_rtu_module.c      <- Seu Modbus RTU
│   ├── modbus_tcp_module.c      <- Wrapper desta lib
│   └── state_machine.c          <- Máquina de estados
└── include/
    ├── wifi_module.h
    ├── modbus_rtu_module.h
    ├── modbus_tcp_module.h
    └── state_machine.h
```

### Exemplo de Wrapper (modbus_tcp_module.c)

```c
#include "modbus_tcp_module.h"
#include "modbus_tcp_slave.h"

static modbus_tcp_handle_t mb_handle = NULL;

esp_err_t modbus_tcp_module_init(esp_netif_t *netif) {
    modbus_tcp_config_t config = {
        .port = 502,
        .slave_id = 1,
        .netif = netif,
        .auto_start = true
    };
    
    return modbus_tcp_slave_init(&config, &mb_handle);
}

esp_err_t modbus_tcp_module_set_sensor_data(float temp, float hum) {
    if (!mb_handle) return ESP_ERR_INVALID_STATE;
    
    ESP_ERROR_CHECK(modbus_tcp_set_input_reg_float(mb_handle, 0, temp));
    ESP_ERROR_CHECK(modbus_tcp_set_input_reg_float(mb_handle, 1, hum));
    
    return ESP_OK;
}

esp_err_t modbus_tcp_module_deinit(void) {
    if (mb_handle) {
        esp_err_t err = modbus_tcp_slave_destroy(mb_handle);
        mb_handle = NULL;
        return err;
    }
    return ESP_OK;
}
```

## 🔧 **API Completa**

### Inicialização e Controle
- `modbus_tcp_slave_init()` - Inicializar biblioteca
- `modbus_tcp_slave_start()` - Iniciar servidor
- `modbus_tcp_slave_stop()` - Parar servidor
- `modbus_tcp_slave_destroy()` - Destruir instância
- `modbus_tcp_slave_get_state()` - Obter estado atual

### Holding Registers (Float)
- `modbus_tcp_set_holding_reg_float()` - Definir valor
- `modbus_tcp_get_holding_reg_float()` - Obter valor

### Input Registers (Float)
- `modbus_tcp_set_input_reg_float()` - Definir valor
- `modbus_tcp_get_input_reg_float()` - Obter valor

### Coils (Boolean)
- `modbus_tcp_set_coil()` - Definir valor
- `modbus_tcp_get_coil()` - Obter valor

### Discrete Inputs (Boolean)
- `modbus_tcp_set_discrete_input()` - Definir valor
- `modbus_tcp_get_discrete_input()` - Obter valor

### Callbacks e Utilitários
- `modbus_tcp_register_callbacks()` - Registrar callbacks
- `modbus_tcp_get_registers_ptr()` - Obter ponteiros diretos
- `modbus_tcp_get_connection_info()` - Info de conexões

## 🧪 **Teste**

### Ferramentas Recomendadas
- **QModMaster** (GUI)
- **ModbusPoll** (Windows)
- **mbpoll** (linha de comando)
- **Python pymodbus**

### Exemplo de Teste
```bash
# Ler holding registers
mbpoll -a 1 -r 1 -c 8 -t 4 192.168.1.99

# Escrever holding register
mbpoll -a 1 -r 1 -t 4 192.168.1.99 1234.5
```

## 📝 **Configurações**

### Parâmetros da Configuração
```c
typedef struct {
    uint16_t port;              // Porta TCP (padrão: 502)
    uint8_t slave_id;           // ID do slave (1-247)
    esp_netif_t *netif;         // Interface de rede
    bool auto_start;            // Auto iniciar
    uint16_t max_connections;   // Máx. conexões (padrão: 5)
    uint32_t timeout_ms;        // Timeout (padrão: 20000ms)
} modbus_tcp_config_t;
```

## 🐛 **Troubleshooting**

### Problemas Comuns

1. **Erro de inicialização**
   - Verifique se a interface de rede está configurada
   - Confirme se a porta não está em uso

2. **Não recebe conexões**
   - Verifique firewall
   - Confirme IP e porta
   - Teste conectividade de rede

3. **Registros não atualizados**
   - Verifique se está chamando as funções de set
   - Confirme se não há erro de endereçamento

## 📄 **Licença**

Apache License 2.0

## 👨‍💻 **Autor**

DPM - Projeto de Automação Industrial

---

Para mais exemplos, veja o arquivo `examples/modbus_tcp_example.h`.