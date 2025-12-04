#ifndef MODBUS_PARAMS_H
#define MODBUS_PARAMS_H

#include <stdint.h>
#include <stdbool.h>
#include "modbus_map.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
    uint8_t discrete_input0;
    uint8_t discrete_input1;
    uint8_t discrete_input2;
    uint8_t discrete_input3;
    uint8_t discrete_input4;
    uint8_t discrete_input5;
    uint8_t discrete_input6;
    uint8_t discrete_input7;
} discrete_reg_params_t;

typedef struct {
    uint32_t holding_data0;
    uint32_t holding_data1;
    float holding_data2;
    float holding_data3;
    float holding_data4;
    float holding_data5;
    float holding_data6;
    float holding_data7;
} holding_reg_params_t;

typedef struct {
    // uint16_t baudrate;
    // uint16_t endereco;
    // uint16_t paridade;
    uint16_t reg1000[REG_CONFIG_SIZE];
} holding_reg1000_params_t;

typedef struct {
    uint8_t coils_port0;
    uint8_t coils_port1;
} coil_reg_params_t;

typedef struct {
    float input_data0;
    float input_data1;
    float input_data2;
    float input_data3;
    float input_data4;
    float input_data5;
    float input_data6;
    float input_data7;
} input_reg_params_t;

extern discrete_reg_params_t discrete_reg_params;
extern holding_reg_params_t holding_reg_params;
extern holding_reg1000_params_t holding_reg1000_params;
extern uint16_t reg2000[REG_DATA_SIZE];
extern uint16_t reg3000[REG_3000_SIZE];
extern uint16_t reg4000[REG_4000_SIZE];
extern uint16_t reg5000[REG_5000_SIZE];
extern uint16_t reg6000[REG_6000_SIZE];
extern uint16_t reg7000[REG_7000_SIZE];
extern uint16_t reg8000[REG_8000_SIZE];
extern uint16_t reg9000[REG_UNITSPECS_SIZE];
extern coil_reg_params_t coil_reg_params;
extern input_reg_params_t input_reg_params;

// ================= CONTROLE DE HABILITAÇÃO RTU =================
// Variável global que controla se Modbus RTU está habilitado
extern bool modbus_rtu_enabled;

// ================= MUTEX DE PROTEÇÃO DOS REGISTRADORES =================
// Mutex para acesso thread-safe aos registradores Modbus compartilhados
// entre RTU e TCP
extern SemaphoreHandle_t modbus_registers_mutex;

/**
 * @brief Inicializa o mutex de proteção dos registradores Modbus
 * @note DEVE ser chamado antes de criar as tasks RTU/TCP
 * @return true se sucesso, false se falha
 */
bool modbus_mutex_init(void);

/**
 * @brief Bloqueia (lock) o acesso aos registradores Modbus
 * @param timeout_ms Timeout em milissegundos (use portMAX_DELAY para espera infinita)
 * @return true se conseguiu o lock, false se timeout
 */
bool modbus_lock_registers(uint32_t timeout_ms);

/**
 * @brief Libera (unlock) o acesso aos registradores Modbus
 */
void modbus_unlock_registers(void);

/**
 * @brief Destrói o mutex (usado no shutdown do sistema)
 */
void modbus_mutex_destroy(void);

#endif // MODBUS_PARAMS_H