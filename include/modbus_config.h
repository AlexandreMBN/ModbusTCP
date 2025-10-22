#ifndef MODBUS_CONFIG_H
#define MODBUS_CONFIG_H

#include <stdint.h>

#define MB_SLAVE_RTU_ENABLED      1
#define MB_SLAVE_ASCII_ENABLED    0
#define MB_SLAVE_TCP_ENABLED      0
// Modbus RTU Slave Configuration
#define MB_PORT_NUM         (2)                // UART port number for Modbus communication
#define MB_SLAVE_ADDR       (1)                // Modbus slave address
#define MB_DEV_SPEED        (115200)             // Baud rate for Modbus communication
#define MB_PARITY           (MB_PARITY_NONE)   // Parity setting for UART communication

// Modbus Register Configuration
#define HOLDING_REG_COUNT   (8)                // Number of holding registers
#define INPUT_REG_COUNT     (8)                // Number of input registers
#define COIL_REG_COUNT      (2)                // Number of coils
#define DISCRETE_INPUT_COUNT (8)               // Number of discrete inputs

// Default values for registers
#define DEFAULT_HOLDING_REG0 (1.0f)
#define DEFAULT_HOLDING_REG1 (2.0f)
#define DEFAULT_INPUT_REG0   (1.0f)
#define DEFAULT_COIL_REG0    (0x00)
#define DEFAULT_DISCRETE_INPUT0 (1)

// Function prototypes for Modbus configuration
void modbus_configure(void);

#endif // MODBUS_CONFIG_H