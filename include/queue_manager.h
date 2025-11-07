/*
 * ========================================================================
 * QUEUE_MANAGER.H - SISTEMA DE FILAS PARA COMUNICAÇÃO INTER-TASKS
 * ========================================================================
 * 
 * Este arquivo define estruturas e funções para comunicação entre tasks
 * usando filas (queues) do FreeRTOS, mantendo compatibilidade com o
 * sistema atual de variáveis globais.
 * 
 * OBJETIVO: Implementar comunicação thread-safe entre tasks usando filas
 * STATUS: Em desenvolvimento - Iniciando com o2Percent
 * 
 * ========================================================================
 */

#ifndef QUEUE_MANAGER_H
#define QUEUE_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdint.h>

// ========== CONFIGURAÇÕES DAS FILAS ==========
#define O2_QUEUE_SIZE 50            // Tamanho da fila para dados de O2 (aumentado para evitar overflow)
#define QUEUE_WAIT_TIME_MS 0        // Tempo de espera (0 = não bloqueia)

// ========== ESTRUTURAS DE MENSAGENS ==========

/**
 * @brief Estrutura para mensagens de dados de O2
 * 
 * Esta estrutura é enviada pela task da sonda para a task Modbus
 * através de uma fila thread-safe.
 */
typedef struct {
    uint16_t o2_percent;        // Valor do percentual de O2 (0-65535)
    uint32_t timestamp;         // Timestamp para debugging (ticks do sistema)
    uint8_t source_task;        // ID da task que enviou (1=sonda, 2=modbus, etc.)
    uint8_t data_valid;         // Flag para indicar se o dado é válido (0/1)
} o2_queue_msg_t;


// Estrutura de dados completa da sonda lambda



// ========== IDENTIFICADORES DE TASKS ==========
typedef enum {
    TASK_ID_UNKNOWN = 0,
    TASK_ID_SONDA = 1,          // Task da sonda lambda
    TASK_ID_MODBUS = 2,         // Task Modbus
    TASK_ID_MQTT = 3,           // Task MQTT  
    TASK_ID_WEBSERVER = 4       // Task WebServer
} task_id_t;

// ========== VARIÁVEIS GLOBAIS DAS FILAS ==========
extern QueueHandle_t o2_data_queue;    // Fila para dados de O2

// ========== FUNÇÕES PÚBLICAS ==========

/**
 * @brief Inicializa todas as filas do sistema
 * @return ESP_OK se sucesso, ESP_ERR_NO_MEM se falha na criação
 */
esp_err_t queue_manager_init(void);

/**
 * @brief Envia dados de O2 para a fila (PRODUTOR)
 * @param o2_value Valor do percentual de O2
 * @param source_id ID da task que está enviando
 * @return ESP_OK se enviado, ESP_ERR_TIMEOUT se fila cheia
 */
esp_err_t queue_send_o2_data(uint16_t o2_value, task_id_t source_id);

/**
 * @brief Recebe dados de O2 da fila (CONSUMIDOR)
 * @param msg Ponteiro para estrutura onde armazenar a mensagem recebida
 * @return ESP_OK se recebido, ESP_ERR_TIMEOUT se fila vazia
 */
esp_err_t queue_receive_o2_data(o2_queue_msg_t *msg);

/**
 * @brief Verifica se há mensagens pendentes na fila de O2
 * @return Número de mensagens na fila
 */
uint32_t queue_get_o2_pending_count(void);

/**
 * @brief Limpa todas as mensagens da fila de O2
 */
void queue_clear_o2_data(void);

#endif // QUEUE_MANAGER_H