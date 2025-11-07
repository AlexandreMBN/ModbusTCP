/*
 * ========================================================================
 * QUEUE_MANAGER.C - IMPLEMENTAÇÃO DO SISTEMA DE FILAS
 * ========================================================================
 * 
 * Este arquivo implementa as funções para gerenciamento de filas do
 * sistema, permitindo comunicação thread-safe entre tasks.
 * 
 * FLUXO IMPLEMENTADO:
 * SONDA TASK → [FILA O2] → MODBUS TASK
 * 
 * ========================================================================
 */

#include "queue_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "QUEUE_MANAGER";

// ========== VARIÁVEIS GLOBAIS DAS FILAS ==========
QueueHandle_t o2_data_queue = NULL;

// ========== IMPLEMENTAÇÃO DAS FUNÇÕES ==========

/**
 * @brief Inicializa todas as filas do sistema
 * 
 * Esta função deve ser chamada uma única vez no início do sistema,
 * preferencialmente no main.c antes de criar as tasks.
 */
esp_err_t queue_manager_init(void) {
    ESP_LOGI(TAG, "🔧 Inicializando sistema de filas...");
    
    // ========== CRIAÇÃO DA FILA DE DADOS O2 ==========
    // Cria fila com capacidade para O2_QUEUE_SIZE mensagens
    // Cada mensagem tem o tamanho de o2_queue_msg_t
    o2_data_queue = xQueueCreate(O2_QUEUE_SIZE, sizeof(o2_queue_msg_t));
    
    if (o2_data_queue == NULL) {
        ESP_LOGE(TAG, "❌ ERRO: Falha ao criar fila de dados O2!");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "✅ Fila O2 criada: %d slots de %d bytes cada", 
             O2_QUEUE_SIZE, sizeof(o2_queue_msg_t));
    
    return ESP_OK;
}

/**
 * @brief Envia dados de O2 para a fila (FUNÇÃO PRODUTORA)
 * 
 * Esta função é chamada pela task da sonda para enviar novos dados
 * de O2 para outras tasks que precisam desses dados.
 */
esp_err_t queue_send_o2_data(uint16_t o2_value, task_id_t source_id) {
    // ========== PREPARAÇÃO DA MENSAGEM ==========
    o2_queue_msg_t msg;
    msg.o2_percent = o2_value;                      // Valor do O2
    msg.timestamp = xTaskGetTickCount();            // Timestamp atual
    msg.source_task = (uint8_t)source_id;          // ID da task origem
    msg.data_valid = (o2_value <= 10000) ? 1 : 0;  // Validação simples
    
    // ========== ENVIO NÃO-BLOQUEANTE ==========
    // xQueueSend com timeout 0 = não bloqueia se a fila estiver cheia
    BaseType_t result = xQueueSend(o2_data_queue, &msg, pdMS_TO_TICKS(QUEUE_WAIT_TIME_MS));
    
    if (result == pdTRUE) {
        ESP_LOGD(TAG, "📤 O2 enviado: %d%% (task_id=%d, timestamp=%lu)", 
                 o2_value, source_id, msg.timestamp);
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "⚠️ Fila O2 cheia! Dados perdidos: %d%%", o2_value);
        return ESP_ERR_TIMEOUT;
    }
}

/**
 * @brief Recebe dados de O2 da fila (FUNÇÃO CONSUMIDORA)
 * 
 * Esta função é chamada pela task Modbus para receber novos dados
 * de O2 enviados pela task da sonda.
 */
esp_err_t queue_receive_o2_data(o2_queue_msg_t *msg) {
    if (msg == NULL) {
        ESP_LOGE(TAG, "❌ ERRO: Ponteiro msg é NULL!");
        return ESP_ERR_INVALID_ARG;
    }
    
    // ========== RECEPÇÃO NÃO-BLOQUEANTE ==========
    // xQueueReceive com timeout 0 = retorna imediatamente se fila vazia
    BaseType_t result = xQueueReceive(o2_data_queue, msg, pdMS_TO_TICKS(QUEUE_WAIT_TIME_MS));
    
    if (result == pdTRUE) {
        ESP_LOGD(TAG, "📥 O2 recebido: %d%% (task_id=%d, timestamp=%lu, válido=%d)", 
                 msg->o2_percent, msg->source_task, msg->timestamp, msg->data_valid);
        return ESP_OK;
    } else {
        // Fila vazia - isso é normal, não é erro
        return ESP_ERR_TIMEOUT;
    }
}

/**
 * @brief Verifica quantas mensagens estão pendentes na fila
 * 
 * Útil para debugging e monitoramento do sistema.
 */
uint32_t queue_get_o2_pending_count(void) {
    if (o2_data_queue == NULL) {
        return 0;
    }
    
    UBaseType_t count = uxQueueMessagesWaiting(o2_data_queue);
    return (uint32_t)count;
}

/**
 * @brief Limpa todas as mensagens da fila de O2
 * 
 * Útil para reset do sistema ou limpeza de dados antigos.
 */
void queue_clear_o2_data(void) {
    if (o2_data_queue == NULL) {
        return;
    }
    
    // Remove todas as mensagens da fila
    xQueueReset(o2_data_queue);
    ESP_LOGI(TAG, "🧹 Fila O2 limpa");
}