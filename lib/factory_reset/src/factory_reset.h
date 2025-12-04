/*
 * ========================================================================
 * FACTORY RESET LIBRARY - BIBLIOTECA PARA RESET DE FÁBRICA
 * ========================================================================
 * 
 * Esta biblioteca centraliza todas as funcionalidades de reset de fábrica
 * do sistema, incluindo reset via botão físico e via interface web.
 * 
 * FUNCIONALIDADES:
 * ================
 * 1. Reset via Botão Físico:
 *    - Monitoramento do botão no GPIO_NUM_4
 *    - Feedback visual via LED no GPIO_NUM_2
 *    - Tempo de pressão configurável (padrão: 3 segundos)
 *    - Task dedicada para monitoramento não-bloqueante
 * 
 * 2. Reset via Interface Web:
 *    - Função para integração com webserver
 *    - Handler HTTP pronto para uso
 *    - Integração com máquina de estados do sistema
 * 
 * 3. Operações de Reset:
 *    - Apagamento completo do NVS
 *    - Remoção de arquivos de configuração SPIFFS
 *    - Reinício controlado do ESP32
 *    - Timeout de segurança
 * 
 * DEPENDÊNCIAS:
 * =============
 * - ESP-IDF: driver/gpio, nvs_flash, esp_log, freertos
 * - Sistema de Eventos: event_bus.h (para integração com máquina estados)
 * - SPIFFS: Para remoção de arquivos de configuração
 * 
 * EXEMPLO DE USO:
 * ===============
 * ```c
 * #include "factory_reset.h"
 * 
 * void app_main() {
 *     // Inicializa biblioteca
 *     factory_reset_init();
 *     
 *     // Inicia monitoramento do botão (opcional)
 *     factory_reset_start_button_monitoring();
 * }
 * 
 * // Para webserver
 * esp_err_t reset_handler(httpd_req_t *req) {
 *     return factory_reset_web_handler(req);
 * }
 * ```
 * 
 * ========================================================================
 */

#ifndef FACTORY_RESET_H
#define FACTORY_RESET_H

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"

// ========================================================================
// CONFIGURAÇÕES DE HARDWARE
// ========================================================================

/**
 * @brief Configurações do botão físico de reset
 */
#define FACTORY_RESET_BUTTON_GPIO       GPIO_NUM_5    ///< Pino do botão de reset
#define FACTORY_RESET_LED_GPIO          GPIO_NUM_2    ///< LED de feedback do reset
#define FACTORY_RESET_BUTTON_PRESS_TIME_MS  3000      ///< Tempo de pressão necessário (3s)
#define FACTORY_RESET_LED_BLINK_PERIOD_MS   200       ///< Período de piscar do LED durante reset

/**
 * @brief Configurações de timeout e segurança
 */
#define FACTORY_RESET_TIMEOUT_MS        20000         ///< Timeout máximo para operação (20s)
#define FACTORY_RESET_DEBOUNCE_MS       50            ///< Debounce do botão (50ms)
#define FACTORY_RESET_TASK_STACK_SIZE   3072          ///< Tamanho da pilha da task de monitoramento

// ========================================================================
// TIPOS E ESTRUTURAS
// ========================================================================

/**
 * @brief Estados do processo de factory reset
 */
typedef enum {
    FACTORY_RESET_STATE_IDLE,           ///< Estado inativo
    FACTORY_RESET_STATE_BUTTON_PRESSED, ///< Botão sendo pressionado
    FACTORY_RESET_STATE_EXECUTING,      ///< Reset sendo executado
    FACTORY_RESET_STATE_COMPLETED,      ///< Reset concluído
    FACTORY_RESET_STATE_ERROR           ///< Erro durante reset
} factory_reset_state_t;

/**
 * @brief Tipos de reset disponíveis
 */
typedef enum {
    FACTORY_RESET_TYPE_BUTTON,          ///< Reset acionado via botão físico
    FACTORY_RESET_TYPE_WEB,             ///< Reset acionado via interface web
    FACTORY_RESET_TYPE_API              ///< Reset acionado via API programática
} factory_reset_type_t;

/**
 * @brief Configuração da biblioteca factory reset
 */
typedef struct {
    gpio_num_t button_gpio;             ///< GPIO do botão (padrão: GPIO_NUM_4)
    gpio_num_t led_gpio;                ///< GPIO do LED (padrão: GPIO_NUM_2)
    uint32_t press_time_ms;             ///< Tempo de pressão necessário (padrão: 3000ms)
    uint32_t debounce_time_ms;          ///< Tempo de debounce (padrão: 50ms)
    bool enable_button_monitoring;      ///< Habilita monitoramento do botão
    bool enable_led_feedback;           ///< Habilita feedback visual via LED
} factory_reset_config_t;

/**
 * @brief Callback para notificação de eventos de reset
 * @param type Tipo de reset que foi executado
 * @param state Estado atual do processo
 */
typedef void (*factory_reset_callback_t)(factory_reset_type_t type, factory_reset_state_t state);

// ========================================================================
// API PÚBLICA
// ========================================================================

/**
 * @brief Inicializa a biblioteca factory reset com configuração padrão
 * 
 * Configura os GPIOs, cria mutex de proteção e prepara sistema.
 * DEVE ser chamada antes de qualquer outra função da biblioteca.
 * 
 * @return ESP_OK se sucesso, ESP_ERR_* em caso de erro
 */
esp_err_t factory_reset_init(void);

/**
 * @brief Inicializa a biblioteca factory reset com configuração customizada
 * 
 * @param config Ponteiro para estrutura de configuração personalizada
 * @return ESP_OK se sucesso, ESP_ERR_INVALID_ARG se config é NULL
 */
esp_err_t factory_reset_init_with_config(const factory_reset_config_t *config);

/**
 * @brief Deinicializa a biblioteca e libera recursos
 * 
 * Para o monitoramento do botão, libera GPIOs e mutex.
 * 
 * @return ESP_OK se sucesso
 */
esp_err_t factory_reset_deinit(void);

// ========================================================================
// CONTROLE DO BOTÃO FÍSICO
// ========================================================================

/**
 * @brief Inicia o monitoramento do botão de reset em task dedicada
 * 
 * Cria uma task que monitora continuamente o estado do botão.
 * Task é automaticamente destruída após reset ser executado.
 * 
 * @return ESP_OK se task foi criada, ESP_ERR_NO_MEM se falha
 */
esp_err_t factory_reset_start_button_monitoring(void);

/**
 * @brief Para o monitoramento do botão de reset
 * 
 * Finaliza a task de monitoramento se estiver rodando.
 * 
 * @return ESP_OK se sucesso
 */
esp_err_t factory_reset_stop_button_monitoring(void);

/**
 * @brief Verifica se o botão está sendo pressionado (leitura única)
 * 
 * Função não-bloqueante para verificação manual do estado do botão.
 * 
 * @return true se botão pressionado, false caso contrário
 */
bool factory_reset_is_button_pressed(void);

// ========================================================================
// EXECUÇÃO DE FACTORY RESET
// ========================================================================

/**
 * @brief Executa factory reset completo de forma síncrona
 * 
 * ATENÇÃO: Esta função é BLOQUEANTE e reinicia o ESP32 ao final!
 * 
 * Operações realizadas:
 * 1. Apaga completamente o NVS
 * 2. Remove arquivos de configuração do SPIFFS
 * 3. Sinaliza máquina de estados (se integrada)
 * 4. Reinicia o ESP32
 * 
 * @param type Tipo de reset sendo executado (para logs/callback)
 * @return ESP_OK se sucesso, ESP_ERR_* se falha (pode não retornar devido ao reboot)
 */
esp_err_t factory_reset_execute(factory_reset_type_t type);

/**
 * @brief Executa factory reset de forma assíncrona
 * 
 * Inicia o processo em task separada. Útil para não bloquear
 * o thread que chama (ex: handler HTTP).
 * 
 * @param type Tipo de reset sendo executado
 * @return ESP_OK se task foi criada, ESP_ERR_* em caso de erro
 */
esp_err_t factory_reset_execute_async(factory_reset_type_t type);

// ========================================================================
// INTEGRAÇÃO COM WEBSERVER
// ========================================================================

#ifdef CONFIG_ESP_HTTP_SERVER_ENABLE
#include "esp_http_server.h"

/**
 * @brief Handler HTTP pronto para uso em webserver
 * 
 * Handler completo para endpoint de factory reset.
 * Retorna resposta HTTP e executa reset de forma assíncrona.
 * 
 * EXEMPLO DE REGISTRO:
 * ```c
 * httpd_uri_t reset_uri = {
 *     .uri = "/factory_reset",
 *     .method = HTTP_POST,
 *     .handler = factory_reset_web_handler
 * };
 * httpd_register_uri_handler(server, &reset_uri);
 * ```
 * 
 * @param req Requisição HTTP
 * @return ESP_OK se sucesso, ESP_ERR_* em caso de erro
 */
esp_err_t factory_reset_web_handler(httpd_req_t *req);

#endif // CONFIG_ESP_HTTP_SERVER_ENABLE

// ========================================================================
// UTILITÁRIOS E CONSULTA DE ESTADO
// ========================================================================

/**
 * @brief Obtém o estado atual do processo de factory reset
 * 
 * @return Estado atual (factory_reset_state_t)
 */
factory_reset_state_t factory_reset_get_state(void);

/**
 * @brief Verifica se um factory reset está em andamento
 * 
 * @return true se reset em progresso, false caso contrário
 */
bool factory_reset_is_in_progress(void);

/**
 * @brief Registra callback para notificação de eventos de reset
 * 
 * @param callback Função a ser chamada em mudanças de estado
 * @return ESP_OK se registrado, ESP_ERR_INVALID_ARG se callback é NULL
 */
esp_err_t factory_reset_register_callback(factory_reset_callback_t callback);

/**
 * @brief Remove callback registrado
 * 
 * @return ESP_OK sempre
 */
esp_err_t factory_reset_unregister_callback(void);

// ========================================================================
// INTEGRAÇÃO COM SISTEMA DE EVENTOS (OPCIONAL)
// ========================================================================

/**
 * @brief Sinaliza início de factory reset para máquina de estados externa
 * 
 * Função de compatibilidade com sistema de eventos existente.
 * Se event_bus.h estiver disponível, envia eventos apropriados.
 * 
 * @return ESP_OK se evento enviado, ESP_ERR_NOT_SUPPORTED se event_bus não disponível
 */
esp_err_t factory_reset_notify_start(void);

/**
 * @brief Sinaliza conclusão de factory reset para máquina de estados externa
 * 
 * @return ESP_OK se evento enviado, ESP_ERR_NOT_SUPPORTED se event_bus não disponível
 */
esp_err_t factory_reset_notify_complete(void);

// ========================================================================
// CONFIGURAÇÕES PADRÃO
// ========================================================================

/**
 * @brief Configuração padrão da biblioteca
 */
#define FACTORY_RESET_DEFAULT_CONFIG() { \
    .button_gpio = FACTORY_RESET_BUTTON_GPIO, \
    .led_gpio = FACTORY_RESET_LED_GPIO, \
    .press_time_ms = FACTORY_RESET_BUTTON_PRESS_TIME_MS, \
    .debounce_time_ms = FACTORY_RESET_DEBOUNCE_MS, \
    .enable_button_monitoring = true, \
    .enable_led_feedback = true \
}

#endif // FACTORY_RESET_H