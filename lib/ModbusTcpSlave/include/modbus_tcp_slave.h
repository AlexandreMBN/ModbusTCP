/**
 * @file modbus_tcp_slave.h
 * @brief Modbus TCP Slave Library for ESP32
 * 
 * Esta biblioteca fornece uma API simples para implementar um Modbus TCP Slave
 * em projetos ESP32 usando o stack FreeModbus.
 */

#ifndef MODBUS_TCP_SLAVE_H
#define MODBUS_TCP_SLAVE_H

#include "esp_err.h"
#include "esp_netif.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handle para instância do Modbus TCP Slave
 */
typedef void* modbus_tcp_handle_t;

/**
 * @brief Estados do Modbus TCP Slave
 */
typedef enum {
    MODBUS_TCP_STATE_STOPPED = 0,
    MODBUS_TCP_STATE_STARTING,
    MODBUS_TCP_STATE_RUNNING,
    MODBUS_TCP_STATE_STOPPING,
    MODBUS_TCP_STATE_ERROR
} modbus_tcp_state_t;

/**
 * @brief Tipos de registros Modbus
 */
typedef enum {
    MODBUS_REG_HOLDING = 0,     ///< Holding Registers (R/W)
    MODBUS_REG_INPUT,           ///< Input Registers (R)
    MODBUS_REG_COIL,            ///< Coils (R/W)
    MODBUS_REG_DISCRETE         ///< Discrete Inputs (R)
} modbus_reg_type_t;

/**
 * @brief Configuração do Modbus TCP Slave
 */
typedef struct {
    uint16_t port;              ///< Porta TCP (padrão: 502)
    uint8_t slave_id;           ///< ID do slave (1-247)
    esp_netif_t *netif;         ///< Interface de rede (pode ser NULL)
    bool auto_start;            ///< Auto iniciar após init
    uint16_t max_connections;   ///< Máximo de conexões simultâneas (padrão: 5)
    uint32_t timeout_ms;        ///< Timeout de conexão em ms (padrão: 20000)
} modbus_tcp_config_t;

/**
 * @brief Estrutura de dados dos registros
 */
typedef struct {
    float holding_data0;
    float holding_data1;
    float holding_data2;
    float holding_data3;
    float holding_data4;
    float holding_data5;
    float holding_data6;
    float holding_data7;
} modbus_holding_regs_t;

typedef struct {
    float input_data0;
    float input_data1;
    float input_data2;
    float input_data3;
    float input_data4;
    float input_data5;
    float input_data6;
    float input_data7;
} modbus_input_regs_t;

typedef struct {
    uint8_t coils_port0;
    uint8_t coils_port1;
} modbus_coil_regs_t;

typedef struct {
    uint8_t discrete_input0:1;
    uint8_t discrete_input1:1;
    uint8_t discrete_input2:1;
    uint8_t discrete_input3:1;
    uint8_t discrete_input4:1;
    uint8_t discrete_input5:1;
    uint8_t discrete_input6:1;
    uint8_t discrete_input7:1;
} modbus_discrete_regs_t;

/**
 * @brief Callbacks para eventos do Modbus
 */
typedef struct {
    /**
     * @brief Callback chamado quando um registro é lido
     * @param addr Endereço do registro
     * @param reg_type Tipo do registro
     * @param value Valor lido (apenas para debug)
     */
    void (*on_register_read)(uint16_t addr, modbus_reg_type_t reg_type, uint32_t value);
    
    /**
     * @brief Callback chamado quando um registro é escrito
     * @param addr Endereço do registro
     * @param reg_type Tipo do registro
     * @param value Novo valor escrito
     */
    void (*on_register_write)(uint16_t addr, modbus_reg_type_t reg_type, uint32_t value);
    
    /**
     * @brief Callback chamado quando há mudança na conexão
     * @param connected true se conectado, false se desconectado
     * @param connection_count Número atual de conexões
     */
    void (*on_connection_change)(bool connected, uint8_t connection_count);
    
    /**
     * @brief Callback chamado quando ocorre erro
     * @param error Código do erro
     * @param description Descrição do erro
     */
    void (*on_error)(esp_err_t error, const char* description);
} modbus_tcp_callbacks_t;

// ================================
// API PRINCIPAL
// ================================

/**
 * @brief Inicializa o Modbus TCP Slave
 * 
 * @param config Configuração do slave
 * @param handle Ponteiro para receber o handle da instância
 * @return esp_err_t 
 */
esp_err_t modbus_tcp_slave_init(const modbus_tcp_config_t *config, modbus_tcp_handle_t *handle);

/**
 * @brief Inicia o servidor Modbus TCP
 * 
 * @param handle Handle da instância
 * @return esp_err_t 
 */
esp_err_t modbus_tcp_slave_start(modbus_tcp_handle_t handle);

/**
 * @brief Para o servidor Modbus TCP
 * 
 * @param handle Handle da instância
 * @return esp_err_t 
 */
esp_err_t modbus_tcp_slave_stop(modbus_tcp_handle_t handle);

/**
 * @brief Destrói a instância do Modbus TCP Slave
 * 
 * @param handle Handle da instância
 * @return esp_err_t 
 */
esp_err_t modbus_tcp_slave_destroy(modbus_tcp_handle_t handle);

/**
 * @brief Obtém o estado atual do slave
 * 
 * @param handle Handle da instância
 * @return modbus_tcp_state_t 
 */
modbus_tcp_state_t modbus_tcp_slave_get_state(modbus_tcp_handle_t handle);

// ================================
// GERENCIAMENTO DE REGISTROS
// ================================

/**
 * @brief Define valor de um Holding Register
 * 
 * @param handle Handle da instância
 * @param addr Endereço do registro (0-7)
 * @param value Valor a ser definido
 * @return esp_err_t 
 */
esp_err_t modbus_tcp_set_holding_reg_float(modbus_tcp_handle_t handle, uint16_t addr, float value);

/**
 * @brief Obtém valor de um Holding Register
 * 
 * @param handle Handle da instância
 * @param addr Endereço do registro (0-7)
 * @param value Ponteiro para receber o valor
 * @return esp_err_t 
 */
esp_err_t modbus_tcp_get_holding_reg_float(modbus_tcp_handle_t handle, uint16_t addr, float *value);

/**
 * @brief Define valor de um Input Register
 * 
 * @param handle Handle da instância
 * @param addr Endereço do registro (0-7)
 * @param value Valor a ser definido
 * @return esp_err_t 
 */
esp_err_t modbus_tcp_set_input_reg_float(modbus_tcp_handle_t handle, uint16_t addr, float value);

/**
 * @brief Obtém valor de um Input Register
 * 
 * @param handle Handle da instância
 * @param addr Endereço do registro (0-7)
 * @param value Ponteiro para receber o valor
 * @return esp_err_t 
 */
esp_err_t modbus_tcp_get_input_reg_float(modbus_tcp_handle_t handle, uint16_t addr, float *value);

// ================================
// FUNÇÕES DE COMPATIBILIDADE RTU (uint16_t)
// ================================

/**
 * @brief Define valor de um Holding Register (compatibilidade RTU)
 * 
 * @param handle Handle da instância
 * @param addr Endereço do registro
 * @param value Valor uint16_t a ser definido
 * @return esp_err_t 
 */
esp_err_t modbus_tcp_set_holding_register(modbus_tcp_handle_t handle, uint16_t addr, uint16_t value);

/**
 * @brief Obtém valor de um Holding Register (compatibilidade RTU)
 * 
 * @param handle Handle da instância
 * @param addr Endereço do registro
 * @param value Ponteiro para receber o valor uint16_t
 * @return esp_err_t 
 */
esp_err_t modbus_tcp_get_holding_register(modbus_tcp_handle_t handle, uint16_t addr, uint16_t *value);

/**
 * @brief Define valor de um Input Register (compatibilidade RTU)
 * 
 * @param handle Handle da instância
 * @param addr Endereço do registro
 * @param value Valor uint16_t a ser definido
 * @return esp_err_t 
 */
esp_err_t modbus_tcp_set_input_register(modbus_tcp_handle_t handle, uint16_t addr, uint16_t value);

/**
 * @brief Obtém valor de um Input Register (compatibilidade RTU)
 * 
 * @param handle Handle da instância
 * @param addr Endereço do registro
 * @param value Ponteiro para receber o valor uint16_t
 * @return esp_err_t 
 */
esp_err_t modbus_tcp_get_input_register(modbus_tcp_handle_t handle, uint16_t addr, uint16_t *value);

/**
 * @brief Define valor de uma Coil
 * 
 * @param handle Handle da instância
 * @param addr Endereço da coil (0-15)
 * @param value Valor a ser definido (true/false)
 * @return esp_err_t 
 */
esp_err_t modbus_tcp_set_coil(modbus_tcp_handle_t handle, uint16_t addr, bool value);

/**
 * @brief Obtém valor de uma Coil
 * 
 * @param handle Handle da instância
 * @param addr Endereço da coil (0-15)
 * @param value Ponteiro para receber o valor
 * @return esp_err_t 
 */
esp_err_t modbus_tcp_get_coil(modbus_tcp_handle_t handle, uint16_t addr, bool *value);

/**
 * @brief Define valor de um Discrete Input
 * 
 * @param handle Handle da instância
 * @param addr Endereço do input (0-7)
 * @param value Valor a ser definido (true/false)
 * @return esp_err_t 
 */
esp_err_t modbus_tcp_set_discrete_input(modbus_tcp_handle_t handle, uint16_t addr, bool value);

/**
 * @brief Obtém valor de um Discrete Input
 * 
 * @param handle Handle da instância
 * @param addr Endereço do input (0-7)
 * @param value Ponteiro para receber o valor
 * @return esp_err_t 
 */
esp_err_t modbus_tcp_get_discrete_input(modbus_tcp_handle_t handle, uint16_t addr, bool *value);

// ================================
// CALLBACKS E EVENTOS
// ================================

/**
 * @brief Registra callbacks para eventos
 * 
 * @param handle Handle da instância
 * @param callbacks Estrutura com os callbacks
 * @return esp_err_t 
 */
esp_err_t modbus_tcp_register_callbacks(modbus_tcp_handle_t handle, const modbus_tcp_callbacks_t *callbacks);

// ================================
// UTILITÁRIOS
// ================================

/**
 * @brief Obtém ponteiros diretos para os registros (uso avançado)
 * 
 * @param handle Handle da instância
 * @param holding Ponteiro para receber holding registers
 * @param input Ponteiro para receber input registers
 * @param coils Ponteiro para receber coils
 * @param discrete Ponteiro para receber discrete inputs
 * @return esp_err_t 
 */
esp_err_t modbus_tcp_get_registers_ptr(modbus_tcp_handle_t handle,
                                       modbus_holding_regs_t **holding,
                                       modbus_input_regs_t **input,
                                       modbus_coil_regs_t **coils,
                                       modbus_discrete_regs_t **discrete);

/**
 * @brief Obtém informações de conexão
 * 
 * @param handle Handle da instância
 * @param connection_count Ponteiro para receber número de conexões
 * @param port Ponteiro para receber porta do servidor
 * @return esp_err_t 
 */
esp_err_t modbus_tcp_get_connection_info(modbus_tcp_handle_t handle, uint8_t *connection_count, uint16_t *port);
void slave_operation_task(void *arg);
#ifdef __cplusplus
}
#endif

#endif // MODBUS_TCP_SLAVE_H