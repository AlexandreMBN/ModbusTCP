/*
 * MODBUS TCP SLAVE TASK HEADER
 * 
 * Implementação de Modbus TCP Slave que compartilha os mesmos registradores
 * com o Modbus RTU existente. Utiliza mutex para sincronização thread-safe.
 * 
 * Características:
 * - Porta TCP: 502 (padrão Modbus TCP)
 * - Protocolo: IPv4 apenas
 * - Registradores compartilhados: reg1000~reg9000 (mesmos do RTU)
 * - Proteção: Mutex para acesso sincronizado
 * - Controle: Habilitado/desabilitado via configuração WebServer
 * 
 * Autor: Sistema de Integração Modbus
 * Data: 12/11/2025
 */

#ifndef MODBUS_TCP_SLAVE_TASK_H
#define MODBUS_TCP_SLAVE_TASK_H

#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ================= CONFIGURAÇÕES DA TASK TCP =================
#define MODBUS_TCP_TASK_STACK_SIZE  4096    // Pilha da task (4KB)
#define MODBUS_TCP_TASK_PRIORITY    3       // Prioridade (menor que RTU=4)
#define MODBUS_TCP_TASK_CORE        0       // Core do processador (0 ou 1)

#define MODBUS_TCP_DEFAULT_PORT     502     // Porta TCP padrão
#define MODBUS_TCP_MAX_CONNECTIONS  5       // Máximo de conexões simultâneas

// ================= ESTADOS DA TASK TCP =================
typedef enum {
    MODBUS_TCP_STATE_STOPPED,      // Task não iniciada
    MODBUS_TCP_STATE_INITIALIZING, // Inicializando stack TCP
    MODBUS_TCP_STATE_RUNNING,      // Rodando normalmente
    MODBUS_TCP_STATE_ERROR,        // Erro crítico
    MODBUS_TCP_STATE_STOPPING      // Parando task
} modbus_tcp_state_t;

// ================= FUNÇÕES PÚBLICAS =================

/**
 * @brief Inicializa e cria a task Modbus TCP Slave
 * 
 * @note Deve ser chamado APÓS modbus_slave_task (RTU) ser criada
 * @note Verifica se TCP está habilitado via configuração antes de criar
 * @return esp_err_t ESP_OK se sucesso, ESP_FAIL se erro
 */
esp_err_t modbus_tcp_slave_init(void);

/**
 * @brief Task principal do Modbus TCP Slave
 * 
 * @param pvParameters Parâmetros da task (não utilizado)
 */
void modbus_tcp_slave_task(void *pvParameters);

/**
 * @brief Inicia o servidor Modbus TCP (se não estiver rodando)
 * 
 * @return esp_err_t ESP_OK se sucesso, ESP_FAIL se erro
 */
esp_err_t modbus_tcp_start(void);

/**
 * @brief Para o servidor Modbus TCP
 * 
 * @return esp_err_t ESP_OK se sucesso, ESP_FAIL se erro
 */
esp_err_t modbus_tcp_stop(void);

/**
 * @brief Obtém o estado atual da task TCP
 * 
 * @return modbus_tcp_state_t Estado atual
 */
modbus_tcp_state_t modbus_tcp_get_state(void);

/**
 * @brief Verifica se o TCP está habilitado na configuração
 * 
 * @return true se habilitado, false se desabilitado
 */
bool modbus_tcp_is_enabled(void);

/**
 * @brief Obtém o handle da task Modbus TCP
 * 
 * @return TaskHandle_t Handle da task ou NULL se não criada
 */
TaskHandle_t modbus_tcp_get_task_handle(void);

/**
 * @brief Destrói a task Modbus TCP e libera recursos
 * 
 * @return esp_err_t ESP_OK se sucesso, ESP_FAIL se erro
 */
esp_err_t modbus_tcp_slave_destroy(void);

#endif // MODBUS_TCP_SLAVE_TASK_H
