# Factory Reset Library

Biblioteca customizada para gerenciamento de factory reset no sistema ESP32.

## Funcionalidades

### 🔘 Reset via Botão Físico
- Monitoramento contínuo do botão no GPIO 4
- Tempo de pressão configurável (padrão: 3 segundos)
- Feedback visual via LED no GPIO 2
- Task dedicada não-bloqueante

### 🌐 Reset via Interface Web
- Handler HTTP pronto para integração
- Execução assíncrona para não bloquear servidor
- Integração com máquina de estados do sistema

### 🛡️ Operações de Reset
- Apagamento completo do NVS
- Remoção automática de arquivos de configuração SPIFFS
- Reinício controlado do ESP32
- Timeout de segurança (20 segundos)

## Uso Básico

```c
#include "factory_reset.h"

void app_main() {
    // Inicializa biblioteca
    factory_reset_init();
    
    // Inicia monitoramento do botão (opcional)
    factory_reset_start_button_monitoring();
}
```

## Integração com WebServer

```c
#include "factory_reset.h"

// Registra endpoint no servidor HTTP
httpd_uri_t reset_uri = {
    .uri = "/factory_reset",
    .method = HTTP_POST,
    .handler = factory_reset_web_handler  // Handler da biblioteca
};
httpd_register_uri_handler(server, &reset_uri);
```

## Configuração Personalizada

```c
factory_reset_config_t custom_config = {
    .button_gpio = GPIO_NUM_0,        // Botão personalizado
    .led_gpio = GPIO_NUM_2,           // LED de feedback
    .press_time_ms = 5000,            // 5 segundos
    .enable_button_monitoring = true,  // Habilita monitoramento
    .enable_led_feedback = true       // Habilita LED
};

factory_reset_init_with_config(&custom_config);
```

## API Principal

| Função | Descrição |
|--------|-----------|
| `factory_reset_init()` | Inicializa com configuração padrão |
| `factory_reset_init_with_config()` | Inicializa com configuração personalizada |
| `factory_reset_start_button_monitoring()` | Inicia monitoramento do botão |
| `factory_reset_execute_async()` | Executa reset de forma assíncrona |
| `factory_reset_web_handler()` | Handler HTTP pronto para uso |
| `factory_reset_get_state()` | Consulta estado atual |

## Estados do Sistema

- `FACTORY_RESET_STATE_IDLE` - Sistema inativo
- `FACTORY_RESET_STATE_BUTTON_PRESSED` - Botão sendo pressionado
- `FACTORY_RESET_STATE_EXECUTING` - Reset sendo executado
- `FACTORY_RESET_STATE_COMPLETED` - Reset concluído
- `FACTORY_RESET_STATE_ERROR` - Erro durante reset

## Hardware

- **Botão**: GPIO 5 (com pull-up interno)
- **LED**: GPIO 2 (feedback visual)
- **Lógica**: Ativo baixo (botão pressionado = nível baixo)

## Dependências

- ESP-IDF Framework
- FreeRTOS
- NVS Flash
- Driver GPIO
- SPIFFS (para remoção de arquivos)
- esp_http_server (opcional, para integração web)

## Integração com Sistema de Eventos

A biblioteca detecta automaticamente se `event_bus.h` está disponível e integra com a máquina de estados do sistema via:

- `factory_reset_notify_start()`
- `factory_reset_notify_complete()`

## Arquivos Removidos no Reset

- `/spiffs/conteudo.json`
- `/spiffs/config.json` 
- `/spiffs/network_config.json`
- Todo o conteúdo do NVS

## Thread Safety

A biblioteca é completamente thread-safe:
- Mutex protege mudanças de estado
- Tasks assíncronas para operações longas
- Callbacks seguros para notificação de eventos