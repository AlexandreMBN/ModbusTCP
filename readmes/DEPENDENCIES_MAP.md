# MAPA DE DEPENDÊNCIAS DO SISTEMA
**Sistema de Controle de Sonda Lambda ESP32**

## ARQUITETURA GERAL

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          SISTEMA PRINCIPAL                              │
│                                                                         │
│  ┌──────────────┐    ┌────────────────┐    ┌─────────────────────────┐  │
│  │    MAIN.C    │───▶│ STATE MACHINE  │───▶│     TASK MANAGER        │  │
│  │ (Inicialização)   │ (Controle fluxo)   │ (Gerenciamento tasks)   │  │
│  └──────────────┘    └────────────────┘    └─────────────────────────┘  │
│           │                   │                         │               │
│           ▼                   ▼                         ▼               │
│  ┌──────────────┐    ┌────────────────┐    ┌─────────────────────────┐  │
│  │ CONFIG_MGR.C │    │ QUEUE_MGR.C    │    │   MODBUS_PARAMS.C       │  │
│  │(Configurações)│    │(Comunicação)   │    │ (Registradores globais) │  │
│  └──────────────┘    └────────────────┘    └─────────────────────────┘  │
│           │                   │                         │               │
│           └───────────────────┼─────────────────────────┘               │
│                               │                                         │
│                               ▼                                         │
│        ┌────────────────────────────────────────────────────────────┐   │
│        │                     TASKS PRINCIPAIS                        │   │
│        └────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
```

## ESTRUTURA DE ARQUIVOS E DEPENDÊNCIAS

### ARQUIVOS PRINCIPAIS (src/)

#### 1. **main.c** - Coordenador Central
```
DEPENDE DE:
├── config_manager.h          → Carregamento de configurações
├── queue_manager.h           → Sistema de filas inter-tasks  
├── wifi_manager.h            → Inicialização WiFi/AP
├── webserver.h               → Servidor web HTTP
├── mqtt_client_task.h        → Cliente MQTT
├── modbus_slave_task.h       → Modbus RTU
├── modbus_tcp_slave_task.h   → Modbus TCP
├── oxygen_sensor_task.h      → Controle sonda lambda
└── event_bus.h               → Sistema de eventos

CONSUMIDO POR:
- Nenhum (arquivo principal)

FUNÇÃO:
- Máquina de estados principal
- Inicialização sequencial do sistema
- Coordenação entre todos os módulos
```

#### 2. **config_manager.c** - Gerenciador de Configurações
```
DEPENDE DE:
├── esp_spiffs.h              → Sistema de arquivos
├── cJSON.h                   → Parser JSON
├── nvs_flash.h               → Backup em NVS
└── modbus_params.h           → Acesso aos registradores

CONSUMIDO POR:
├── main.c                    → Inicialização
├── webserver.c               → Interface web
├── wifi_manager.c            → Configs AP/STA
├── mqtt_client_task.c        → Config MQTT
├── modbus_slave_task.c       → Config RTU
└── modbus_tcp_slave_task.c   → Config TCP

FUNÇÃO:
- Persistência de todas as configurações
- Sistema duplo SPIFFS + NVS
- APIs modulares por tipo de config
```

#### 3. **queue_manager.c** - Sistema de Filas
```
DEPENDE DE:
├── freertos/queue.h          → Filas FreeRTOS
└── queue_manager.h           → Definições

CONSUMIDO POR:
├── oxygen_sensor_task.c      → Envio dados O2
├── mqtt_client_task.c        → Recepção dados O2
└── modbus_slave_task.c       → Comunicação registradores

FUNÇÃO:
- Comunicação assíncrona entre tasks
- Thread-safe messaging
- Desacoplamento entre produtores/consumidores
```

#### 4. **modbus_params.c** - Registradores Globais
```
DEPENDE DE:
├── freertos/semphr.h         → Mutex para thread-safety
└── modbus_params.h           → Definições dos registradores

CONSUMIDO POR:
├── oxygen_sensor_task.c      → Escrita valores sonda
├── modbus_slave_task.c       → Leitura/escrita Modbus RTU
├── modbus_tcp_slave_task.c   → Leitura/escrita Modbus TCP
├── webserver.c               → Interface web
└── config_manager.c          → Persistência configurações

FUNÇÃO:
- Arrays globais de registradores Modbus
- Proteção mutex para acesso concorrente
- Mapeamento 1000/2000/4000/6000/9000
```

### TASKS ESPECIALIZADAS

#### 5. **oxygen_sensor_task.c** - Controle da Sonda
```
DEPENDE DE:
├── lib/cj125/                → Driver controlador sonda
├── lib/adcRio/               → ADC calibrado
├── lib/PID/                  → Controlador PID
├── lib/sonda/                → Algoritmos específicos
├── queue_manager.h           → Envio dados via fila
└── modbus_params.h           → Escrita registradores

CONSUMIDO POR:
- Nenhum (task independente)

FUNÇÃO:
- Controle PID de temperatura (450°C)
- Medição lambda e conversão para %O2
- Atualização registradores 4000/6000
- Envio dados para MQTT via fila
```

#### 6. **mqtt_client_task.c** - Cliente MQTT
```
DEPENDE DE:
├── esp_mqtt_client.h         → Cliente MQTT ESP-IDF
├── config_manager.h          → Config MQTT
├── queue_manager.h           → Recepção dados O2
└── cJSON.h                   → Formatação JSON

CONSUMIDO POR:
├── main.c                    → Inicialização
└── webserver.c               → Status e controle

FUNÇÃO:
- Conexão com broker MQTT
- Publicação dados sonda em tempo real
- Reconexão automática
- Integração com sistema de filas
```

#### 7. **modbus_slave_task.c** - Servidor Modbus RTU
```
DEPENDE DE:
├── mbcontroller.h            → Stack Modbus ESP-IDF
├── modbus_params.h           → Registradores globais
├── config_manager.h          → Config serial
└── queue_manager.h           → Comunicação com outras tasks

CONSUMIDO POR:
├── main.c                    → Criação da task
└── webserver.c               → Status Modbus

FUNÇÃO:
- Servidor Modbus RTU via RS485
- Mapeamento registradores para protocolo
- Resposta a comandos externos
- Configuração dinâmica de baudrate/paridade
```

#### 8. **modbus_tcp_slave_task.c** - Servidor Modbus TCP
```
DEPENDE DE:
├── mbcontroller.h            → Stack Modbus TCP
├── modbus_params.h           → Registradores compartilhados
├── config_manager.h          → Config TCP (porta, conexões)
└── esp_netif.h               → Interface de rede

CONSUMIDO POR:
├── main.c                    → Inicialização condicional
└── webserver.c               → Configuração via web

FUNÇÃO:
- Servidor Modbus TCP via Ethernet/WiFi
- Múltiplas conexões simultâneas
- Mesmo mapeamento que RTU
- Habilitação/desabilitação dinâmica
```

#### 9. **webserver.c** - Interface Web
```
DEPENDE DE:
├── esp_http_server.h         → Servidor HTTP
├── esp_spiffs.h              → Arquivos web (HTML/CSS/JS)
├── config_manager.h          → Todas as configurações
├── wifi_manager.h            → Status WiFi e scan
├── modbus_params.h           → Leitura registradores
├── mqtt_client_task.h        → Status/controle MQTT
└── cJSON.h                   → APIs REST JSON

CONSUMIDO POR:
├── main.c                    → Inicialização
└── wifi_manager.c            → Callback de conexão

FUNÇÃO:
- Interface web responsiva
- APIs REST para configuração
- Monitoramento em tempo real
- Controle de acesso por níveis
- Factory reset e diagnósticos
```

#### 10. **wifi_manager.c** - Gerenciamento WiFi
```
DEPENDE DE:
├── esp_wifi.h                → Stack WiFi ESP-IDF
├── esp_netif.h               → Interface de rede
├── config_manager.h          → Configs AP/STA/Network
└── webserver.h               → Inicialização servidor

CONSUMIDO POR:
└── main.c                    → Inicialização do sistema

FUNÇÃO:
- Modo dual AP + STA simultâneo
- Auto-conexão com credenciais salvas
- Configuração IP estático/DHCP
- Fallback automático para AP
```

## FLUXOS DE DADOS PRINCIPAIS

### 1. **Fluxo de Dados da Sonda**
```
[CJ125] → [oxygen_sensor_task.c] → [modbus_params.c] → [modbus_slave_task.c] → [Cliente Modbus]
                    ↓
               [queue_manager.c] → [mqtt_client_task.c] → [Broker MQTT]
                    ↓
               [webserver.c] ← [Browser Web]
```

### 2. **Fluxo de Configuração**
```
[Interface Web] → [webserver.c] → [config_manager.c] → [SPIFFS + NVS]
                                         ↓
[Tasks] ← [main.c] ← [Recarregamento] ← [config_manager.c]
```

### 3. **Fluxo de Inicialização**
```
[main.c] → [config_manager] → [queue_manager] → [WiFi] → [WebServer] → [MQTT] → [Tasks]
```

## BIBLIOTECAS EXTERNAS (lib/)

```
lib/
├── cj125/          → Driver SPI para controlador sonda
├── adcRio/         → ADC calibrado ESP32
├── PID/            → Controlador PID genérico
├── sonda/          → Algoritmos específicos da sonda
├── dacMC/          → Driver DAC para controle
├── wifi_manager/   → Manager WiFi avançado
├── mqtt_client_manager/ → Cliente MQTT robusto
├── config_manager/ → Gerenciador de configs
├── spiffs_file_manager/ → Gerenciador SPIFFS
└── web_server_manager/  → Servidor web modular
```

## HEADERS COMPARTILHADOS (include/)

```
include/
├── modbus_params.h      → Definições registradores globais
├── config_manager.h     → Estruturas de configuração  
├── queue_manager.h      → APIs do sistema de filas
├── event_bus.h          → Sistema de eventos global
├── wifi_manager.h       → APIs WiFi
├── webserver.h          → APIs servidor web
└── *_task.h             → Protótipos de cada task
```

## PONTOS DE INTEGRAÇÃO CRÍTICOS

### 1. **Sistema de Filas (queue_manager.c)**
- **Produtores**: oxygen_sensor_task.c
- **Consumidores**: mqtt_client_task.c, modbus_slave_task.c
- **Tipo**: Assíncrono, thread-safe
- **Capacidade**: Configurável via macros

### 2. **Registradores Modbus (modbus_params.c)**
- **Escritores**: oxygen_sensor_task.c, webserver.c
- **Leitores**: modbus_slave_task.c, modbus_tcp_slave_task.c, webserver.c
- **Proteção**: Mutex obrigatório
- **Sincronização**: Tempo real

### 3. **Configurações (config_manager.c)**
- **Escritores**: webserver.c (via interface)
- **Leitores**: Todas as tasks na inicialização
- **Persistência**: SPIFFS (primário) + NVS (backup)
- **Formato**: JSON estruturado

### 4. **Máquina de Estados (main.c)**
- **Controla**: Sequência de inicialização
- **Monitora**: Status de todas as tasks
- **Recupera**: Erros automáticos (3 tentativas)
- **Logs**: Todos os eventos e transições

## ESTATÍSTICAS DO PROJETO

- **Total de arquivos .c**: 12
- **Total de arquivos .h**: 15+  
- **Bibliotecas externas**: 10+
- **Tasks FreeRTOS**: 6-8 (dependendo da configuração)
- **Endpoints HTTP**: 25+
- **Registradores Modbus**: 5 faixas (1000, 2000, 4000, 6000, 9000)
- **Configurações JSON**: 6 tipos
- **Protocolos suportados**: HTTP, MQTT, Modbus RTU, Modbus TCP, WiFi

## COMPILAÇÃO E BUILD

O sistema utiliza **PlatformIO** com as seguintes dependências principais:
- ESP-IDF framework
- FreeRTOS (incluído no ESP-IDF)
- cJSON para parsing JSON
- Drivers SPI/ADC/DAC customizados
- Stack Modbus ESP-IDF

### Ordem de Compilação:
1. Bibliotecas (lib/)
2. Headers (include/) 
3. Arquivos principais (src/)
4. Linkagem final

---
*Documentação gerada automaticamente - Sistema de Controle Sonda Lambda ESP32*