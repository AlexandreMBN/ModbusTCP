/**
 * @file modbus_manager.h
 * @brief Sistema de gerenciamento e alternância entre Modbus RTU e TCP
 * 
 * Este módulo fornece uma interface unificada para gerenciar tanto
 * Modbus RTU (ESP-IDF nativo) quanto Modbus TCP (biblioteca customizada),
 * permitindo alternância dinâmica entre os modos usando os mesmos registradores.
 * 
 * FUNCIONALIDADES:
 * ---------------
 * - Alternância dinâmica RTU ↔ TCP sem reiniciar ESP32
 * - Sincronização automática de todos os registradores
 * - Fallback inteligente (RTU quando WiFi não disponível)
 * - Interface de controle via web
 * - Monitoramento de status em tempo real
 * 
 * ARQUITETURA:
 * ------------
 *  ┌─────────────────────────────────────┐
 *  │         MODBUS MANAGER              │
 *  │  (Controla RTU ↔ TCP + Registros)  │
 *  └─────────────────┬───────────────────┘
 *                    │
 *        ┌───────────┴───────────┐
 *        │                       │
 *    ┌───▼────┐             ┌────▼─────┐
 *    │  RTU   │             │   TCP    │
 *    │(ESP-IDF│             │(Biblioteca│
 *    │ Nativo)│             │ Custom)  │
 *    └────────┘             └──────────┘
 * 
 * EXEMPLO DE USO:
 * ---------------
 * ```c
 * // Inicialização (no main.c)
 * xTaskCreate(modbus_manager_task, "Modbus Manager", 4096, NULL, 5, NULL);
 * 
 * // Alternância via API
 * modbus_manager_switch_mode(MODBUS_MODE_TCP);
 * 
 * // Consulta de status
 * modbus_mode_t current = modbus_manager_get_mode();
 * bool is_active = modbus_manager_is_running();
 * ```
 * 
 * @author Sistema ESP32
 * @date 2025
 */

#ifndef MODBUS_MANAGER_H
#define MODBUS_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// Dependências das implementações Modbus
#include "modbus_tcp_slave.h"    // Biblioteca TCP customizada
#include "esp_modbus_slave.h"    // ESP-IDF RTU nativo

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * TIPOS E ENUMERAÇÕES
 * ============================================================================ */

/**
 * @brief Modos de operação do Modbus
 * 
 * MODBUS_MODE_DISABLED: Nenhum protocolo ativo (economia de energia)
 * MODBUS_MODE_RTU:      Modbus RTU via serial (ESP-IDF nativo)  
 * MODBUS_MODE_TCP:      Modbus TCP via WiFi (biblioteca customizada)
 * MODBUS_MODE_AUTO:     Auto-seleção baseada na conectividade WiFi
 */
typedef enum {
    MODBUS_MODE_DISABLED = 0,   // Modbus completamente desabilitado
    MODBUS_MODE_RTU      = 1,   // RTU via serial (sempre disponível)
    MODBUS_MODE_TCP      = 2,   // TCP via WiFi (requer conectividade)
    MODBUS_MODE_AUTO     = 3    // TCP se WiFi OK, senão RTU
} modbus_mode_t;

/**
 * @brief Estados internos do gerenciador
 * 
 * Usado para controle interno da máquina de estados do manager
 */
typedef enum {
    MANAGER_STATE_INITIALIZING,    // Inicializando recursos
    MANAGER_STATE_IDLE,           // Pronto mas sem protocolo ativo
    MANAGER_STATE_RUNNING_RTU,    // RTU ativo e operacional
    MANAGER_STATE_RUNNING_TCP,    // TCP ativo e operacional 
    MANAGER_STATE_SWITCHING,      // Em processo de alternância
    MANAGER_STATE_ERROR           // Erro - aguardando recuperação
} modbus_manager_state_t;

/**
 * @brief Status detalhado do sistema Modbus
 * 
 * Estrutura retornada pelas funções de consulta de status
 */
typedef struct {
    modbus_mode_t mode;              // Modo configurado pelo usuário
    modbus_manager_state_t state;    // Estado interno atual
    bool is_running;                 // Se algum protocolo está ativo
    bool wifi_available;             // Se WiFi está conectado
    uint32_t uptime_seconds;         // Tempo desde última alternância
    uint32_t rtu_message_count;      // Mensagens RTU processadas
    uint32_t tcp_connection_count;   // Conexões TCP ativas
    esp_err_t last_error;           // Último erro registrado
    char error_description[64];      // Descrição do último erro
} modbus_status_t;

/**
 * @brief Configuração do gerenciador Modbus
 * 
 * Permite personalizar comportamento do manager
 */
typedef struct {
    uint32_t sync_interval_ms;       // Intervalo de sincronização (padrão: 1000ms)
    uint32_t wifi_check_interval_ms; // Intervalo de verificação WiFi (padrão: 5000ms)
    bool auto_fallback_enabled;     // Se deve fazer fallback RTU quando WiFi cai
    bool register_sync_enabled;      // Se deve sincronizar registradores
    uint8_t max_retry_attempts;      // Tentativas de recuperação de erro
} modbus_manager_config_t;

/* ============================================================================
 * API PÚBLICA - FUNÇÕES PRINCIPAIS  
 * ============================================================================ */

/**
 * @brief Inicializa o sistema de gerenciamento Modbus
 * 
 * DEVE ser chamado antes de usar qualquer outra função.
 * Cria mutex, inicializa estruturas internas e lê configuração.
 * 
 * @param config Configuração do manager (NULL para padrões)
 * @return ESP_OK em sucesso, código de erro em falha
 */
esp_err_t modbus_manager_init(const modbus_manager_config_t *config);

/**
 * @brief Função principal da task do gerenciador
 * 
 * Esta é a task principal que deve ser criada no main.c.
 * Gerencia alternância de modos, sincronização e monitoramento.
 * 
 * Exemplo:
 * xTaskCreate(modbus_manager_task, "Modbus Manager", 4096, NULL, 5, NULL);
 * 
 * @param pvParameters Parâmetros da task (não usado - passar NULL)
 */
void modbus_manager_task(void *pvParameters);

/**
 * @brief Alterna para um modo específico de operação
 * 
 * Função thread-safe para solicitar mudança de modo.
 * A mudança é processada de forma assíncrona pela task manager.
 * 
 * @param new_mode Novo modo desejado
 * @return ESP_OK se solicitação aceita, erro se modo inválido
 */
esp_err_t modbus_manager_switch_mode(modbus_mode_t new_mode);

/**
 * @brief Obtém o modo atual de operação  
 * 
 * @return Modo atual (thread-safe)
 */
modbus_mode_t modbus_manager_get_mode(void);

/**
 * @brief Verifica se algum protocolo Modbus está ativo
 * 
 * @return true se RTU ou TCP estiver rodando
 */
bool modbus_manager_is_running(void);

/**
 * @brief Obtém status detalhado do sistema
 * 
 * @param status Ponteiro para estrutura que receberá o status
 * @return ESP_OK em sucesso
 */
esp_err_t modbus_manager_get_status(modbus_status_t *status);

/* ============================================================================
 * API PÚBLICA - FUNÇÕES DE CONFIGURAÇÃO
 * ============================================================================ */

/**
 * @brief Lê configuração do modo Modbus do arquivo config.json
 * 
 * @return Modo configurado (MODBUS_MODE_RTU se não encontrado)
 */
modbus_mode_t modbus_manager_read_config_mode(void);

/**
 * @brief Salva configuração do modo Modbus no config.json
 * 
 * @param mode Modo a ser salvo na configuração
 * @return ESP_OK em sucesso, código de erro em falha
 */
esp_err_t modbus_manager_save_config_mode(modbus_mode_t mode);

/* ============================================================================
 * API PÚBLICA - FUNÇÕES DE SINCRONIZAÇÃO (Implementadas posteriormente)
 * ============================================================================ */

/**
 * @brief Força sincronização manual dos registradores
 * 
 * Útil após alterações manuais nos registradores via código.
 * Normalmente a sincronização é automática.
 * 
 * @return ESP_OK em sucesso
 */
esp_err_t modbus_manager_sync_registers(void);

/**
 * @brief Configura callback para notificação de mudança de modo
 * 
 * @param callback Função a ser chamada quando modo muda
 * @return ESP_OK em sucesso
 */
typedef void (*modbus_mode_change_callback_t)(modbus_mode_t old_mode, modbus_mode_t new_mode);
esp_err_t modbus_manager_set_mode_callback(modbus_mode_change_callback_t callback);

/* ============================================================================
 * API INTERNA - NÃO USAR FORA DO MÓDULO
 * ============================================================================ */

/**
 * @brief Parada de emergência de todos os protocolos
 * 
 * Função interna para situações de erro crítico.
 * USO EXTERNO NÃO RECOMENDADO.
 * 
 * @return ESP_OK em sucesso
 */
esp_err_t modbus_manager_emergency_stop(void);

/* ============================================================================
 * CONSTANTES E CONFIGURAÇÕES PADRÃO
 * ============================================================================ */

// Configurações padrão do gerenciador
#define MODBUS_MANAGER_DEFAULT_SYNC_INTERVAL_MS      1000   // 1 segundo
#define MODBUS_MANAGER_DEFAULT_WIFI_CHECK_INTERVAL_MS 5000  // 5 segundos  
#define MODBUS_MANAGER_DEFAULT_MAX_RETRY_ATTEMPTS    3      // 3 tentativas
#define MODBUS_MANAGER_TASK_STACK_SIZE               4096   // Tamanho da pilha
#define MODBUS_MANAGER_TASK_PRIORITY                 5      // Prioridade média

// Timeouts para operações críticas
#define MODBUS_MANAGER_MODE_SWITCH_TIMEOUT_MS        10000  // 10s para alternância
#define MODBUS_MANAGER_RTU_INIT_TIMEOUT_MS          5000   // 5s para iniciar RTU
#define MODBUS_MANAGER_TCP_INIT_TIMEOUT_MS          15000  // 15s para iniciar TCP

#ifdef __cplusplus
}
#endif

#endif // MODBUS_MANAGER_H