/**
 * @file modbus_tcp_example.h
 * @brief Exemplo de como usar a biblioteca ModbusTcpSlave
 */

#ifndef MODBUS_TCP_EXAMPLE_H
#define MODBUS_TCP_EXAMPLE_H

#include "modbus_tcp_slave.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Exemplo completo de uso da biblioteca Modbus TCP Slave
 * 
 * Este exemplo mostra como:
 * 1. Inicializar a biblioteca
 * 2. Configurar callbacks
 * 3. Gerenciar registros
 * 4. Integrar com sistema modular
 */

// Função de exemplo para inicializar Modbus TCP
esp_err_t example_init_modbus_tcp(esp_netif_t *netif);

// Callbacks de exemplo
void example_on_register_read(uint16_t addr, modbus_reg_type_t reg_type, uint32_t value);
void example_on_register_write(uint16_t addr, modbus_reg_type_t reg_type, uint32_t value);
void example_on_connection_change(bool connected, uint8_t connection_count);
void example_on_error(esp_err_t error, const char* description);

// Exemplo de uso em task separada
void example_modbus_tcp_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // MODBUS_TCP_EXAMPLE_H

/*
========================================
EXEMPLO DE USO NO SEU PROJETO PRINCIPAL:
========================================

#include "modbus_tcp_slave.h"

static modbus_tcp_handle_t mb_tcp_handle = NULL;

// 1. INICIALIZAÇÃO (chamada após WiFi conectar)
esp_err_t init_modbus_module(void) {
    modbus_tcp_config_t config = {
        .port = 502,                    // Porta padrão Modbus TCP
        .slave_id = 1,                  // ID do slave
        .netif = get_sta_netif(),       // Sua função de rede
        .auto_start = true,             // Iniciar automaticamente
        .max_connections = 5,           // Máximo 5 conexões
        .timeout_ms = 20000            // Timeout de 20s
    };
    
    // Configurar callbacks
    modbus_tcp_callbacks_t callbacks = {
        .on_register_read = my_on_register_read,
        .on_register_write = my_on_register_write,
        .on_connection_change = my_on_connection_change,
        .on_error = my_on_error
    };
    
    // Inicializar
    ESP_ERROR_CHECK(modbus_tcp_slave_init(&config, &mb_tcp_handle));
    ESP_ERROR_CHECK(modbus_tcp_register_callbacks(mb_tcp_handle, &callbacks));
    
    ESP_LOGI("MODBUS", "Modbus TCP module initialized successfully");
    return ESP_OK;
}

// 2. CALLBACKS CUSTOMIZADOS
void my_on_register_write(uint16_t addr, modbus_reg_type_t reg_type, uint32_t value) {
    ESP_LOGI("MODBUS", "Register written - Type: %d, Addr: %d, Value: %lu", 
             reg_type, addr, value);
    
    // Exemplo: Se escreveram no holding register 0, fazer algo
    if (reg_type == MODBUS_REG_HOLDING && addr == 0) {
        // Acionar alguma função do seu sistema
        trigger_system_action();
    }
}

void my_on_connection_change(bool connected, uint8_t connection_count) {
    ESP_LOGI("MODBUS", "Connection changed - Connected: %s, Count: %d", 
             connected ? "YES" : "NO", connection_count);
}

// 3. ATUALIZAÇÃO DE DADOS (chamada periodicamente)
void update_modbus_data(float sensor1, float sensor2, bool alarm1, bool alarm2) {
    if (!mb_tcp_handle) return;
    
    // Atualizar input registers com dados dos sensores
    modbus_tcp_set_input_reg_float(mb_tcp_handle, 0, sensor1);
    modbus_tcp_set_input_reg_float(mb_tcp_handle, 1, sensor2);
    
    // Atualizar discrete inputs com alarmes
    modbus_tcp_set_discrete_input(mb_tcp_handle, 0, alarm1);
    modbus_tcp_set_discrete_input(mb_tcp_handle, 1, alarm2);
}

// 4. INTEGRAÇÃO COM MÁQUINA DE ESTADOS
typedef enum {
    STATE_INIT,
    STATE_WIFI_CONNECTING,
    STATE_WIFI_CONNECTED,
    STATE_MODBUS_STARTING,
    STATE_MODBUS_RUNNING,
    STATE_ERROR
} system_state_t;

void system_state_machine(system_state_t *current_state) {
    switch (*current_state) {
        case STATE_WIFI_CONNECTED:
            // WiFi conectou, inicializar Modbus
            if (init_modbus_module() == ESP_OK) {
                *current_state = STATE_MODBUS_RUNNING;
            } else {
                *current_state = STATE_ERROR;
            }
            break;
            
        case STATE_MODBUS_RUNNING:
            // Sistema rodando, atualizar dados
            update_modbus_data(get_sensor1(), get_sensor2(), 
                             get_alarm1(), get_alarm2());
            break;
            
        // ... outros estados
    }
}

// 5. FINALIZAÇÃO (chamada no shutdown)
void deinit_modbus_module(void) {
    if (mb_tcp_handle) {
        modbus_tcp_slave_destroy(mb_tcp_handle);
        mb_tcp_handle = NULL;
        ESP_LOGI("MODBUS", "Modbus TCP module deinitialized");
    }
}

========================================
ESTRUTURA DO SEU PROJETO COM A LIB:
========================================

seu_projeto/
├── lib/
│   └── ModbusTcpSlave/         <- Copia da biblioteca
├── src/
│   ├── main.c                  <- Seu main principal
│   ├── wifi_module.c           <- Seu módulo WiFi
│   ├── modbus_rtu_module.c     <- Seu Modbus RTU
│   ├── modbus_tcp_module.c     <- Wrapper da nossa lib
│   └── state_machine.c         <- Sua máquina de estados
├── include/
│   ├── wifi_module.h
│   ├── modbus_rtu_module.h
│   ├── modbus_tcp_module.h     <- Interface do módulo TCP
│   └── state_machine.h
└── platformio.ini

========================================
PLATFORMIO.INI DO SEU PROJETO:
========================================

[env:esp32dev]
platform = espressif32
board = esp32dev
framework = espidf
lib_deps = 
    ; Suas outras dependências
lib_extra_dirs = lib

; A biblioteca será encontrada automaticamente em lib/ModbusTcpSlave/

*/