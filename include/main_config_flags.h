#ifndef MAIN_CONFIG_FLAGS_H
#define MAIN_CONFIG_FLAGS_H

#include <stdbool.h>

/**
 * @brief Estrutura global para controle de recursos via main_config.json
 * 
 * PROPÓSITO:
 * Esta estrutura permite habilitar/desabilitar recursos do sistema
 * dinamicamente através do arquivo /spiffs/config/main_config.json,
 * sem necessidade de recompilar o firmware.
 * 
 * EXEMPLO DE main_config.json:
 * {
 *   "rtu_enabled": false,    // Modbus RTU (Serial RS485)
 *   "tcp_enabled": false,    // Modbus TCP (Ethernet/WiFi)
 *   "AP_enabled": true,      // WiFi Access Point
 *   "sta_enabled": true,     // WiFi Station (Cliente)
 *   "log_main_flags": false, // Logs da máquina de estados
 *   "log_sonda_queue": false,// Logs de fila da sonda
 *   "log_sonda_values": false,// Logs de valores da sonda
 *   "log_modbus_tcp": false  // Logs de operações Modbus TCP
 * }
 */
typedef struct {
    // Flags de recursos/módulos
    bool rtu_enabled;      // Habilita Modbus RTU (Serial RS485)
    bool tcp_enabled;      // Habilita Modbus TCP (Ethernet/WiFi)
    bool AP_enabled;       // Habilita WiFi Access Point
    bool sta_enabled;      // Habilita WiFi Station (Cliente)
    bool web_enabled;      // Habilita Servidor Web HTTP
    
    // Flags de controle de logs
    bool log_main_flags;   // Habilita logs "Flags carregadas" do MAIN
    bool log_sonda_queue;  // Habilita logs de fila da sonda (tentativa envio, falhas)
    bool log_sonda_values; // Habilita logs de valores (heat, erro, lambda, O2, u)
    bool log_modbus_tcp;   // Habilita logs de leitura Modbus TCP (HOLDING READ)
} main_flags_t;

// Declaração externa da instância global (definida em main.c)
extern main_flags_t FLAGS;

#endif // MAIN_CONFIG_FLAGS_H
