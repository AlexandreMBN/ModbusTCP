#include "modbus_params.h"


// Definições globais reais (alocação de memória)
discrete_reg_params_t discrete_reg_params;
holding_reg_params_t  holding_reg_params;
holding_reg1000_params_t  holding_reg1000_params;
uint16_t reg2000[REG_DATA_SIZE]; //2000
uint16_t reg3000[REG_3000_SIZE];
uint16_t reg4000[REG_4000_SIZE];
uint16_t reg5000[REG_5000_SIZE];
uint16_t reg6000[REG_6000_SIZE];
uint16_t reg7000[REG_7000_SIZE];
uint16_t reg8000[REG_8000_SIZE];
uint16_t reg9000[REG_UNITSPECS_SIZE];
coil_reg_params_t     coil_reg_params;
input_reg_params_t    input_reg_params;