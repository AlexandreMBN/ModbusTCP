/* ============================================================================
 * MAIN.C - SISTEMA DE CONTROLE DE SONDA LAMBDA COM MÁQUINA DE ESTADOS
 * ============================================================================
 * 
 * DESCRIÇÃO:
 * -----------
 * Este arquivo implementa o controle principal de um sistema embarcado para
 * monitoramento de sonda lambda (sensor de oxigênio) com integração Modbus,
 * WiFi, WebServer e MQTT. Utiliza uma máquina de estados para gerenciar a
 * inicialização e operação de todas as tasks do sistema de forma ordenada.
 * 
 * ARQUITETURA DO SISTEMA:
 * -----------------------
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │                      MÁQUINA DE ESTADOS PRINCIPAL                    │
 * │  (Gerencia inicialização sequencial e comunicação entre tasks)     │
 * └─────────────────────────────────────────────────────────────────────┘
 *          │
 *          ├──> 1. NVS Setup (Validação de armazenamento persistente)
 *          ├──> 2. WiFi Init (AP + STA simultâneo)
 *          ├──> 3. WebServer Start (Interface web de configuração)
 *          ├──> 4. MQTT Init (Publicação de dados na nuvem)
 *          ├──> 5. Tasks Start (Tasks principais do sistema)
 *          │    ├─> Modbus Slave (Comunicação Modbus RTU/TCP)
 *          │    ├─> Sonda Control (Leitura e controle do sensor O2)
 *          │    └─> MQTT Client (Publicação contínua de dados)
 *          └──> 6. Running (Sistema operacional)
 * 
 * TASKS DO SISTEMA (em ordem de prioridade):
 * ------------------------------------------
 * 1. State Machine Task     - Prioridade: MÁXIMA (controle do sistema)
 * 2. Sonda Control Task     - Prioridade: 10 (leitura tempo-real do sensor)
 * 3. MQTT Client Task       - Prioridade: 6  (publicação de dados)
 * 4. WiFi/WebServer Tasks   - Prioridade: 5  (interface de usuário)
 * 5. Modbus Slave Task      - Prioridade: 3  (comunicação industrial)
 * 
 * FLUXO DE DADOS:
 * ---------------
 * [Sensor O2] → [Sonda Control] → [Queue Manager] → [Modbus Slave]
 *                                        ↓
 *                                   [MQTT Client] → [Broker MQTT]
 *                                        ↓
 *                                   [WebServer] → [Interface Web]
 * 
 * SISTEMA DE COMUNICAÇÃO INTER-TASKS:
 * -----------------------------------
 * - Queue Manager: Sistema de filas FreeRTOS para comunicação assíncrona
 * - Event Queue: Fila de eventos para controle da máquina de estados
 * - Mutex: Proteção de acesso ao estado do sistema
 * 
 * ESTADOS DA MÁQUINA DE ESTADOS:
 * ------------------------------
 * STATE_INIT            → Inicialização básica do sistema
 * STATE_NVS_SETUP       → Validação do NVS (leitura/escrita/integridade)
 * STATE_WIFI_INIT       → Inicialização WiFi (AP/STA)
 * STATE_WEBSERVER_START → Inicialização do servidor web
 * STATE_MQTT_INIT       → Inicialização cliente MQTT
 * STATE_TASKS_START     → Criação das tasks principais
 * STATE_RUNNING         → Sistema em operação normal
 * STATE_ERROR           → Estado de erro (tentativa de recuperação)
 * 
 * EVENTOS DO SISTEMA:
 * -------------------
 * EVENT_INIT_COMPLETE    → Inicialização básica concluída
 * EVENT_NVS_READY        → NVS validado e operacional (teste de escrita OK)
 * EVENT_WIFI_READY       → WiFi conectado/AP ativo
 * EVENT_WEBSERVER_READY  → WebServer iniciado
 * EVENT_MQTT_READY       → Cliente MQTT conectado
 * EVENT_TASKS_READY      → Tasks principais criadas
 * EVENT_ERROR_OCCURRED   → Erro detectado (prioridade alta)
 * 
 * REFERÊNCIAS DE ARQUIVOS RELACIONADOS:
 * -------------------------------------
 * - modbus_slave_task.c    : Implementação Modbus RTU/TCP
 * - oxygen_sensor_task.c   : Controle da sonda lambda (renamed: sonda_control_task)
 * - mqtt_client_task.c     : Cliente MQTT para publicação de dados
 * - wifi_manager.c         : Gerenciamento WiFi (AP + STA)
 * - webserver.c            : Servidor web (configuração e monitoramento)
 * - queue_manager.c        : Sistema de filas para comunicação inter-tasks
 * - config_manager.c       : Gerenciamento de configurações (NVS + SPIFFS)
 * 
 * CONFIGURAÇÕES IMPORTANTES:
 * --------------------------
 * - Tamanho da pilha das tasks: Configurado por task (2-8KB)
 * - Fila de eventos: 10 eventos simultâneos
 * - Timeout de inicialização WiFi: 10 segundos
 * - Log level padrão: INFO (DEBUG para debug de filas)
 * 
 * ============================================================================
 */

/* ============================================================================
 * SEÇÃO 1: INCLUDES E DEPENDÊNCIAS
 * ============================================================================
 * Organização dos includes por categoria para facilitar manutenção
 */

// ──────────────────────────────────────────────────────────────────────────
// 1.1 BIBLIOTECAS PADRÃO C
// ──────────────────────────────────────────────────────────────────────────
#include <stdio.h>
#include <stdbool.h>

// ──────────────────────────────────────────────────────────────────────────
// 1.2 FREERTOS (Sistema operacional em tempo real)
// ──────────────────────────────────────────────────────────────────────────
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"           // Gerenciamento de tasks
#include "freertos/queue.h"          // Filas para comunicação inter-tasks
#include "freertos/semphr.h"         // Semáforos e mutex
#include "FreeRTOSConfig.h"

// ──────────────────────────────────────────────────────────────────────────
// 1.3 ESP-IDF (Framework Espressif)
// ──────────────────────────────────────────────────────────────────────────
#include <nvs_flash.h>               // Armazenamento não-volátil
#include <esp_log.h>                 // Sistema de logs
#include <esp_event.h>               // Sistema de eventos
#include <esp_netif.h>               // Interface de rede
#include <esp_wifi.h>                // WiFi
#include <esp_spiffs.h>              // Sistema de arquivos SPIFFS
#include "soc/soc.h"                 // Registradores do SoC
#include "soc/rtc_cntl_reg.h"        // Controle RTC (brownout)
#include "cJSON.h"                   // Parser JSON

// ──────────────────────────────────────────────────────────────────────────
// 1.4 MODBUS (Comunicação industrial)
// ──────────────────────────────────────────────────────────────────────────
#include "mbcontroller.h"            // Controlador Modbus
#include "modbus_slave_task.h"       // Task Modbus RTU (Serial)
#include "modbus_tcp_slave_task.h"   // Task Modbus TCP (Ethernet/WiFi)
#include "modbus_task.h"             // Funções auxiliares Modbus

// ──────────────────────────────────────────────────────────────────────────
// 1.5 TASKS DO SISTEMA (nossos módulos)
// ──────────────────────────────────────────────────────────────────────────
#include "oxygen_sensor_task.h"      // Task de controle da sonda lambda
#include "mqtt_client_task.h"        // Task cliente MQTT
#include "other_task.h"              // Tasks auxiliares

// ──────────────────────────────────────────────────────────────────────────
// 1.6 GERENCIADORES DO SISTEMA (nossos módulos)
// ──────────────────────────────────────────────────────────────────────────
#include "queue_manager.h"           // Sistema de filas inter-tasks
#include "wifi_manager.h"            // Gerenciamento WiFi (AP + STA)
#include "webserver.h"                // Servidor web HTTP

// ──────────────────────────────────────────────────────────────────────────
// 1.7 DECLARAÇÕES FORWARD (funções definidas em outros arquivos)
// ──────────────────────────────────────────────────────────────────────────
void load_coils_from_nvs(void);      // Carrega coils Modbus do NVS
void load_holding_regs_from_nvs(void); // Carrega registradores Modbus do NVS

/* ============================================================================
 * SEÇÃO 2: CONSTANTES E DEFINIÇÕES
 * ============================================================================
 */

// ──────────────────────────────────────────────────────────────────────────
// CONFIG FLAGS (GLOBAL) - Sistema de Controle de Recursos
// ──────────────────────────────────────────────────────────────────────────
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
 *   "sta_enabled": true      // WiFi Station (Cliente)
 * }
 * 
 * COMPORTAMENTO:
 * - Se o arquivo não existir: Usa valores padrão seguros (AP=true, resto=false)
 * - Se JSON inválido: Usa valores padrão e loga erro
 * - Se chave ausente no JSON: Mantém valor padrão dessa flag
 * 
 * VALIDAÇÕES:
 * - AP_enabled e sta_enabled não podem ser ambos false (sistema inacessível)
 * - Flags são carregadas ANTES de qualquer inicialização de hardware
 * 
 * INTEGRAÇÃO COM MÓDULOS:
 * - wifi_manager.c: Usa FLAGS.AP_enabled e FLAGS.sta_enabled
 * - modbus_slave_task.c: Usa FLAGS.rtu_enabled
 * - modbus_tcp_slave_task.c: Usa FLAGS.tcp_enabled
 */
typedef struct {
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

// Instância global das flags (acessível por todos os módulos)
main_flags_t FLAGS;

// ──────────────────────────────────────────────────────────────────────────
// 2.1 TAG PARA LOGS
// ──────────────────────────────────────────────────────────────────────────
static const char *TAG = "MAIN";

// ──────────────────────────────────────────────────────────────────────────
// 2.2 FUNÇÃO AUXILIAR PARA MONTAR SPIFFS (Sistema de Arquivos)
// ──────────────────────────────────────────────────────────────────────────
/**
 * @brief Garante que o sistema de arquivos SPIFFS esteja montado
 * 
 * PROBLEMA RESOLVIDO:
 * Múltiplos módulos precisam do SPIFFS (main.c, webserver.c, config_manager.c).
 * Se cada um tentar registrar o SPIFFS, o segundo retorna ESP_ERR_INVALID_STATE
 * e causava abort() com ESP_ERROR_CHECK.
 * 
 * SOLUÇÃO:
 * Esta função centraliza a montagem do SPIFFS e trata ESP_ERR_INVALID_STATE
 * como SUCESSO (significa que outro módulo já montou).
 * 
 * FUNCIONAMENTO:
 * 1. Verifica flag estática 'mounted' (evita chamadas repetidas)
 * 2. Tenta registrar SPIFFS em /spiffs
 * 3. Se retornar ESP_ERR_INVALID_STATE → OK (já montado por outro módulo)
 * 4. Se retornar outro erro → Propaga erro
 * 5. Se sucesso → Marca 'mounted = true' e retorna OK
 * 
 * CONFIGURAÇÃO SPIFFS:
 * - base_path: "/spiffs" (prefixo de todos os arquivos)
 * - partition_label: NULL (usa primeira partição SPIFFS da tabela)
 * - max_files: 10 (até 10 arquivos abertos simultaneamente)
 * - format_if_mount_failed: true (formata se corrompido)
 * 
 * INTEGRAÇÃO:
 * - Chamada ANTES de qualquer leitura de arquivo JSON
 * - Usada por: load_main_config_flags(), config_manager.c, webserver.c
 * 
 * @return ESP_OK se montado com sucesso ou já estava montado
 * @return ESP_ERR_* em caso de erro real (memória, hardware, etc)
 */
static esp_err_t ensure_spiffs_mounted(void) {
    static bool mounted = false;
    if (mounted) {
        ESP_LOGD(TAG, "SPIFFS já montado anteriormente");
        return ESP_OK;
    }
    
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 10,
        .format_if_mount_failed = true,
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    
    // ESP_ERR_INVALID_STATE significa que já foi registrado por outro módulo (webserver.c)
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGD(TAG, "SPIFFS já registrado por outro módulo");
        mounted = true;
        return ESP_OK;
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao montar SPIFFS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    mounted = true;
    ESP_LOGI(TAG, "SPIFFS montado com sucesso em /spiffs");
    return ESP_OK;
}

// ──────────────────────────────────────────────────────────────────────────
// 2.3 CONFIGURAÇÕES DE HARDWARE (Botão de Reset - FUNCIONALIDADE FUTURA)
// ──────────────────────────────────────────────────────────────────────────
// NOTA: Funcionalidade de reset por botão físico desabilitada temporariamente
#define RESET_BUTTON_GPIO GPIO_NUM_4              // Pino do botão de reset
#define RESET_BUTTON_PRESS_TIME_MS 3000           // Tempo de pressão (3s)
#define RESET_LED_GPIO GPIO_NUM_2                 // LED de feedback do reset

// ──────────────────────────────────────────────────────────────────────────
// 2.3 TIMEOUTS DOS ESTADOS DE INICIALIZAÇÃO (em milissegundos)
// ──────────────────────────────────────────────────────────────────────────
/**
 * @brief Timeouts para detectar travamentos durante inicialização
 * 
 * Se um estado demorar mais que o timeout, o sistema:
 * - Registra erro no log
 * - Transita para STATE_ERROR
 * - Tenta recuperação ou reinicia
 */
#define TIMEOUT_NVS_INIT_MS         5000    // 5s para validar NVS
#define TIMEOUT_WIFI_INIT_MS        30000   // 30s para WiFi/AP inicializar
#define TIMEOUT_WEBSERVER_INIT_MS   10000   // 10s para WebServer iniciar
#define TIMEOUT_MQTT_INIT_MS        5000    // 5s para MQTT conectar (reduzido para teste)
#define TIMEOUT_TASKS_START_MS      5000    // 5s para criar todas as tasks
#define TIMEOUT_FACTORY_RESET_MS    20000   // 20s para concluir factory reset

// Intervalo de verificação de timeout (verificado a cada loop)
#define TIMEOUT_CHECK_INTERVAL_MS   100     // Verifica timeout a cada 100ms

/* ============================================================================
 * SEÇÃO 3: TIPOS E ESTRUTURAS DE DADOS
 * ============================================================================
 */

// ──────────────────────────────────────────────────────────────────────────
// 3.1 ESTADOS DA MÁQUINA DE ESTADOS
// ──────────────────────────────────────────────────────────────────────────
/**
 * @brief Estados do sistema durante inicialização e operação
 * 
 * Fluxo normal de estados:
 * INIT → NVS_SETUP → WIFI_INIT → WEBSERVER_START → MQTT_INIT → 
 * TASKS_START → RUNNING
 * 
 * Em caso de erro crítico:
 * qualquer estado → ERROR → tenta recuperar até 3x → INIT (ou esp_restart())
 */
typedef enum {
    STATE_INIT,              // Estado inicial do sistema
    STATE_NVS_SETUP,         // Configurando armazenamento NVS
    STATE_WIFI_INIT,         // Inicializando WiFi (AP + STA)
    STATE_WEBSERVER_START,   // Iniciando servidor web
    STATE_MQTT_INIT,         // Inicializando cliente MQTT
    STATE_TASKS_START,       // Criando tasks principais (Modbus, Sonda)
    STATE_RUNNING,           // Sistema operacional (estado final)
    STATE_BUSY_FACTORY_RESET,// Ocupado executando factory reset
    STATE_ERROR              // Estado de erro com recuperação automática (até 3 tentativas)
} system_state_t;

// ──────────────────────────────────────────────────────────────────────────
// 3.2 EVENTOS DO SISTEMA
// ──────────────────────────────────────────────────────────────────────────
/**
 * @brief Eventos que causam transições de estado
 * 
 * Eventos normais (sequenciais):
 * - INIT_COMPLETE → NVS_READY → WIFI_READY → WEBSERVER_READY → 
 *   MQTT_READY → TASKS_READY
 * 
 * Eventos prioritários (interrompem fluxo normal):
 * - ERROR_OCCURRED: Processado antes de qualquer outro evento
 */
typedef enum {
    EVENT_INIT_COMPLETE,       // Inicialização básica concluída
    EVENT_NVS_READY,           // NVS inicializado
    EVENT_WIFI_READY,          // WiFi conectado ou AP ativo
    EVENT_WEBSERVER_READY,     // WebServer iniciado
    EVENT_MQTT_READY,          // Cliente MQTT conectado (ou falhou)
    EVENT_TASKS_READY,         // Tasks principais criadas
    EVENT_FACTORY_RESET_START, // Início de Factory Reset (PRIORIDADE NORMAL)
    EVENT_FACTORY_RESET_COMPLETE, // Conclusão de Factory Reset
    EVENT_ERROR_OCCURRED       // Erro crítico detectado (PRIORIDADE)
} system_event_t;

// ──────────────────────────────────────────────────────────────────────────
// 3.3 CONTROLE DE TASKS
// ──────────────────────────────────────────────────────────────────────────
/**
 * @brief Estrutura para armazenar handles de todas as tasks do sistema
 * 
 * Permite monitorar status e controlar lifecycle das tasks
 * NULL indica que a task não foi criada ou já foi deletada
 */
typedef struct {
    TaskHandle_t modbus_rtu_task_handle;     // Task Modbus RTU (Serial RS485)
    TaskHandle_t modbus_tcp_task_handle;     // Task Modbus TCP (Ethernet/WiFi)
    TaskHandle_t sonda_control_task_handle;  // Task controle sonda O2
    TaskHandle_t wifi_task_handle;           // Task init WiFi (temporária)
    TaskHandle_t webserver_task_handle;      // Task init WebServer (temporária)
    TaskHandle_t mqtt_task_handle;           // Task cliente MQTT
    TaskHandle_t state_machine_handle;       // Task máquina de estados (esta)
} task_handles_t;

// ──────────────────────────────────────────────────────────────────────────
// 3.4 PROCESSADOR DE EVENTOS MÚLTIPLOS
// ──────────────────────────────────────────────────────────────────────────
/**
 * @brief Estrutura para processar múltiplos eventos simultaneamente
 * 
 * Permite processar vários eventos de uma vez usando bitmaps,
 * com priorização de eventos críticos (ERROR_OCCURRED)
 */
typedef struct {
    uint32_t event_flags;              // Bitmap de todos os eventos pendentes
    uint32_t priority_events;          // Bitmap de eventos prioritários
    system_event_t events_buffer[10];  // Buffer temporário de eventos
    uint8_t event_count;               // Contador de eventos pendentes
} event_processor_t;

/* ============================================================================
 * SEÇÃO 4: VARIÁVEIS GLOBAIS
 * ============================================================================
 */

// ──────────────────────────────────────────────────────────────────────────
// 4.1 ESTADO E CONTROLE DA MÁQUINA DE ESTADOS
// ──────────────────────────────────────────────────────────────────────────
static system_state_t current_state = STATE_INIT;  // Estado atual do sistema
static QueueHandle_t event_queue = NULL;           // Fila de eventos
static task_handles_t task_handles = {0};          // Handles das tasks
static SemaphoreHandle_t state_mutex = NULL;       // Mutex para acesso ao estado
static event_processor_t event_processor = {0};    // Processador de eventos

// ──────────────────────────────────────────────────────────────────────────
// 4.2 CONTROLE DE RESET (FUNCIONALIDADE FUTURA)
// ──────────────────────────────────────────────────────────────────────────
volatile bool reset_pending = false;  // Flag para reset de fábrica pendente

// ──────────────────────────────────────────────────────────────────────────
// 4.3 CONTROLE DE TIMEOUT DOS ESTADOS BUSY
// ──────────────────────────────────────────────────────────────────────────
/**
 * @brief Contadores de tempo para detectar timeouts em estados BUSY
 * 
 * Cada estado de inicialização tem seu próprio contador.
 * São incrementados a cada ciclo da máquina de estados e
 * resetados quando o estado muda ou operação completa.
 */
static uint32_t nvs_init_time_ms = 0;           // Tempo em STATE_NVS_SETUP
static uint32_t wifi_init_time_ms = 0;          // Tempo em STATE_WIFI_INIT
static uint32_t webserver_init_time_ms = 0;     // Tempo em STATE_WEBSERVER_START
static uint32_t mqtt_init_time_ms = 0;          // Tempo em STATE_MQTT_INIT
static uint32_t tasks_start_time_ms = 0;        // Tempo em STATE_TASKS_START
static uint32_t factory_reset_time_ms = 0;      // Tempo em STATE_BUSY_FACTORY_RESET

// Timestamp da última verificação (para calcular delta)
static TickType_t last_state_tick = 0;

// Contador de tentativas de recuperação de erro
static uint8_t error_recovery_count = 0;        // Número de tentativas de recuperação
static const uint8_t MAX_RECOVERY_ATTEMPTS = 3; // Máximo de 3 tentativas antes de reiniciar ESP32

// ──────────────────────────────────────────────────────────────────────────
// 4.4 BITMASKS PARA EVENTOS (otimização de processamento)
// ──────────────────────────────────────────────────────────────────────────
#define EVENT_MASK_INIT_COMPLETE    (1 << EVENT_INIT_COMPLETE)
#define EVENT_MASK_NVS_READY        (1 << EVENT_NVS_READY)
#define EVENT_MASK_WIFI_READY       (1 << EVENT_WIFI_READY)
#define EVENT_MASK_WEBSERVER_READY  (1 << EVENT_WEBSERVER_READY)
#define EVENT_MASK_MQTT_READY       (1 << EVENT_MQTT_READY)
#define EVENT_MASK_TASKS_READY      (1 << EVENT_TASKS_READY)
#define EVENT_MASK_FACTORY_RESET_START    (1 << EVENT_FACTORY_RESET_START)
#define EVENT_MASK_FACTORY_RESET_COMPLETE (1 << EVENT_FACTORY_RESET_COMPLETE)
#define EVENT_MASK_ERROR_OCCURRED   (1 << EVENT_ERROR_OCCURRED)

// Eventos que têm prioridade máxima (processados antes dos outros)
#define PRIORITY_EVENTS_MASK (EVENT_MASK_ERROR_OCCURRED)

/* ============================================================================
 * SEÇÃO 5: TASKS DE INICIALIZAÇÃO (WRAPPERS)
 * ============================================================================
 * Tasks temporárias criadas durante a inicialização do sistema.
 * Após completarem suas funções, enviam eventos e se auto-deletam.
 */

// ──────────────────────────────────────────────────────────────────────────
// 5.1 TASK DE VALIDAÇÃO DO NVS
// ──────────────────────────────────────────────────────────────────────────
/**
 * @brief Task temporária para validar inicialização e acesso ao NVS
 * 
 * FUNCIONAMENTO:
 * 1. Verifica se NVS está acessível abrindo uma partição de teste
 * 2. Valida operações básicas de leitura/escrita
 * 3. Envia EVENT_NVS_READY ou EVENT_ERROR_OCCURRED
 * 4. Se auto-deleta
 * 
 * TESTES REALIZADOS:
 * - Abertura de namespace NVS
 * - Tentativa de leitura de chave (verifica integridade)
 * - Fechamento correto do handle
 * 
 * DEPENDÊNCIAS:
 * - nvs_flash_init() deve ter sido chamado antes
 * 
 * @param param Não utilizado (NULL)
 */
static void nvs_validation_task(void *param) {
    ESP_LOGI(TAG, "NVS Validation Task iniciada");
    
    system_event_t event;
    nvs_handle_t nvs_handle;
    esp_err_t err;
    
    // ─────────────────────────────────────────────────────────────────────
    // Teste 1: Tentar abrir namespace NVS
    // ─────────────────────────────────────────────────────────────────────
    err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao abrir NVS namespace: %s", esp_err_to_name(err));
        event = EVENT_ERROR_OCCURRED;
        xQueueSend(event_queue, &event, portMAX_DELAY);
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "NVS namespace aberto com sucesso");
    
    // ─────────────────────────────────────────────────────────────────────
    // Teste 2: Verificar integridade básica (tentativa de leitura)
    // ─────────────────────────────────────────────────────────────────────
    uint8_t test_value = 0;
    err = nvs_get_u8(nvs_handle, "nvs_test", &test_value);
    
    // É normal não existir a chave ainda - o importante é o NVS responder
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "NVS com problema de integridade: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        event = EVENT_ERROR_OCCURRED;
        xQueueSend(event_queue, &event, portMAX_DELAY);
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "NVS passou no teste de integridade");
    
    // ─────────────────────────────────────────────────────────────────────
    // Teste 3: Escrever valor de teste (valida escrita)
    // ─────────────────────────────────────────────────────────────────────
    err = nvs_set_u8(nvs_handle, "nvs_test", 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao escrever no NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        event = EVENT_ERROR_OCCURRED;
        xQueueSend(event_queue, &event, portMAX_DELAY);
        vTaskDelete(NULL);
        return;
    }
    
    // Commit das mudanças
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao fazer commit no NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        event = EVENT_ERROR_OCCURRED;
        xQueueSend(event_queue, &event, portMAX_DELAY);
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "NVS operação de escrita bem sucedida");
    
    // Fecha handle
    nvs_close(nvs_handle);
    
    // ─────────────────────────────────────────────────────────────────────
    // NVS validado com sucesso - envia evento
    // ─────────────────────────────────────────────────────────────────────
    ESP_LOGI(TAG, "NVS totalmente validado e operacional");
    event = EVENT_NVS_READY;
    xQueueSend(event_queue, &event, portMAX_DELAY);
    
    ESP_LOGI(TAG, "NVS Validation Task finalizada");
    vTaskDelete(NULL);  // Auto-deleta após conclusão
}

// ──────────────────────────────────────────────────────────────────────────
// 5.2 TASK DE INICIALIZAÇÃO DO WiFi
// ──────────────────────────────────────────────────────────────────────────
/**
 * @brief Task temporária para inicializar WiFi em AP e/ou STA
 * 
 * FUNCIONAMENTO:
 * 1. Chama start_wifi_ap() do wifi_manager
 * 2. Aguarda até 10s para WiFi/AP ficar pronto
 * 3. Envia EVENT_WIFI_READY ou EVENT_ERROR_OCCURRED
 * 4. Se auto-deleta
 * 
 * DEPENDÊNCIAS:
 * - wifi_manager.c: start_wifi_ap(), wifi_is_initialized(), wifi_get_status()
 * - NVS deve estar inicializado (credenciais WiFi salvas)
 * 
 * @param param Não utilizado (NULL)
 */
static void wifi_init_task(void *param) {
    ESP_LOGI(TAG, "WiFi Init Task iniciada");
    
    // ─────────────────────────────────────────────────────────────────────
    // Verifica configuração das flags
    // ─────────────────────────────────────────────────────────────────────
    ESP_LOGI(TAG, "Configuração WiFi: AP=%s, STA=%s", 
             FLAGS.AP_enabled ? "SIM" : "NAO",
             FLAGS.sta_enabled ? "SIM" : "NAO");
    
    // Se ambos desabilitados, erro crítico (não tem como acessar o sistema)
    if (!FLAGS.AP_enabled && !FLAGS.sta_enabled) {
        ESP_LOGE(TAG, "ERRO: AP e STA desabilitados! Sistema inacessível!");
        system_event_t event = EVENT_ERROR_OCCURRED;
        xQueueSend(event_queue, &event, portMAX_DELAY);
        vTaskDelete(NULL);
        return;
    }
    
    // Configura log level para debug detalhado
    esp_log_level_set("*", ESP_LOG_INFO);
    
    // ─────────────────────────────────────────────────────────────────────
    // CRÍTICO: Configura flag de STA no wifi_manager ANTES de inicializar WiFi
    // ─────────────────────────────────────────────────────────────────────
    // CONTEXTO DO PROBLEMA:
    // O STA (Station Mode) não estava iniciando de forma confiável. Em alguns
    // boots, o ESP32 conectava ao roteador, em outros não. Era necessário
    // reiniciar o ESP32 várias vezes até funcionar.
    //
    // CAUSA RAIZ IDENTIFICADA:
    // O evento WIFI_EVENT_STA_START não chamava esp_wifi_connect() quando a
    // flag sta_enabled estava false no momento da inicialização. Como a flag
    // era lida DEPOIS do WiFi iniciar, o comportamento era inconsistente.
    //
    // SOLUÇÃO:
    // Chamar wifi_set_sta_enabled(FLAGS.sta_enabled) ANTES de start_wifi_ap()
    // garante que o wifi_manager.c saiba se deve auto-conectar o STA ou não.
    //
    // FLUXO CORRETO:
    // 1. main.c carrega FLAGS do main_config.json
    // 2. main.c chama wifi_set_sta_enabled(FLAGS.sta_enabled)  ← AQUI
    // 3. main.c chama start_wifi_ap()
    // 4. wifi_manager.c recebe WIFI_EVENT_STA_START
    // 5. wifi_manager.c verifica sta_enabled_flag (já configurada)
    // 6. Se true: chama esp_wifi_connect() automaticamente
    // 7. Se false: apenas loga e não conecta
    //
    // RESULTADO:
    // - STA inicia de forma 100% confiável quando sta_enabled=true
    // - Sistema respeita a configuração do JSON sem necessidade de restart
    // - Retry automático com backoff exponencial (até 5 tentativas)
    wifi_set_sta_enabled(FLAGS.sta_enabled);
    
    // ─────────────────────────────────────────────────────────────────────
    // Inicia WiFi conforme configuração das flags
    // ─────────────────────────────────────────────────────────────────────
    if (FLAGS.AP_enabled && FLAGS.sta_enabled) {
        ESP_LOGI(TAG, "Iniciando WiFi em modo DUAL (AP + STA)...");
        start_wifi_ap();  // Função já inicia AP + STA se houver credenciais
    }
    else if (FLAGS.AP_enabled) {
        ESP_LOGI(TAG, "Iniciando WiFi em modo AP APENAS...");
        start_wifi_ap();  // TODO: Criar função específica start_wifi_ap_only()
    }
    else if (FLAGS.sta_enabled) {
        ESP_LOGI(TAG, "Iniciando WiFi em modo STA APENAS...");
        start_wifi_ap();  // TODO: Criar função específica start_wifi_sta_only()
    }
    
    ESP_LOGI(TAG, "start_wifi_ap() retornou");
    
    // ─────────────────────────────────────────────────────────────────────
    // Aguarda WiFi/AP inicializar com timeout de 10 segundos
    // ─────────────────────────────────────────────────────────────────────
    ESP_LOGI(TAG, "Aguardando WiFi inicializar...");
    int wait_ms = 0;
    const int timeout_ms = 10000;
    bool wifi_ready = false;
    
    while (wait_ms < timeout_ms) {
        if (wifi_is_initialized()) {
            wifi_status_t st = wifi_get_status();
            
            // WiFi pronto se pelo menos um dos modos habilitados estiver ativo
            if ((FLAGS.AP_enabled && st.ap_active) || 
                (FLAGS.sta_enabled && st.is_connected)) {
                ESP_LOGI(TAG, "WiFi pronto: AP=%s, STA=%s", 
                         st.ap_active ? "ATIVO" : "inativo",
                         st.is_connected ? "CONECTADO" : "desconectado");
                wifi_ready = true;
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        wait_ms += 200;
    }
    
    // ─────────────────────────────────────────────────────────────────────
    // Envia evento de sucesso ou erro para a máquina de estados
    // ─────────────────────────────────────────────────────────────────────
    system_event_t event = wifi_ready ? EVENT_WIFI_READY : EVENT_ERROR_OCCURRED;
    xQueueSend(event_queue, &event, portMAX_DELAY);
    
    ESP_LOGI(TAG, "WiFi Init Task finalizada");
    vTaskDelete(NULL);  // Auto-deleta após conclusão
}

// ──────────────────────────────────────────────────────────────────────────
// 5.3 TASK DE INICIALIZAÇÃO DO WEBSERVER
// ──────────────────────────────────────────────────────────────────────────
/**
 * @brief Task temporária para inicializar servidor web HTTP
 * 
 * FUNCIONAMENTO:
 * 1. Chama start_web_server() do webserver.c
 * 2. Verifica retorno (sucesso ou erro)
 * 3. Envia EVENT_WEBSERVER_READY (sucesso) ou EVENT_ERROR_OCCURRED (falha)
 * 4. Se auto-deleta
 * 
 * DEPENDÊNCIAS:
 * - webserver.c: start_web_server()
 * - WiFi deve estar ativo (AP ou STA)
 * - SPIFFS deve estar montado (arquivos HTML/CSS/JS)
 * 
 * POSSÍVEIS ERROS:
 * - SPIFFS não montado
 * - Memória insuficiente
 * - Porta 80 já em uso
 * 
 * INTERFACE WEB:
 * - http://192.168.4.1 (AP mode)
 * - http://<IP_STA> (STA mode)
 * 
 * @param param Não utilizado (NULL)
 */
static void webserver_init_task(void *param) {
    ESP_LOGI(TAG, "WebServer Init Task iniciada");
    
    ESP_LOGI(TAG, "Chamando start_web_server()...");
    esp_err_t ret = start_web_server();
    
    // Envia evento apropriado (sucesso ou erro)
    system_event_t event = (ret == ESP_OK) ? EVENT_WEBSERVER_READY : EVENT_ERROR_OCCURRED;
    xQueueSend(event_queue, &event, portMAX_DELAY);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar WebServer: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "WebServer iniciado com sucesso");
    }
    
    ESP_LOGI(TAG, "WebServer Init Task finalizada");
    vTaskDelete(NULL);  // Auto-deleta após conclusão
}

// ──────────────────────────────────────────────────────────────────────────
// 5.4 TASK DE INICIALIZAÇÃO DO MQTT
// ──────────────────────────────────────────────────────────────────────────
/**
 * @brief Task temporária para inicializar cliente MQTT
 * 
 * FUNCIONAMENTO:
 * 1. Aguarda 2s para WiFi estabilizar
 * 2. Chama mqtt_init() para configurar cliente
 * 3. Chama mqtt_start() para conectar ao broker
 * 4. Envia EVENT_MQTT_READY (sucesso) ou EVENT_ERROR_OCCURRED (falha)
 * 5. Se auto-deleta
 * 
 * DEPENDÊNCIAS:
 * - mqtt_client_task.c: mqtt_init(), mqtt_start()
 * - WiFi STA deve estar conectado (acesso à internet)
 * - Broker MQTT configurado (padrão: broker.hivemq.com)
 * 
 * NOTA: Sistema é resiliente - continua funcionando mesmo se MQTT falhar
 * 
 * @param param Não utilizado (NULL)
 */
static void mqtt_init_task(void *param) {
    ESP_LOGI(TAG, "MQTT Init Task iniciada");
    
    // Aguarda WiFi estabilizar antes de conectar ao MQTT
    ESP_LOGI(TAG, "⏳ Aguardando 2s para WiFi estabilizar...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    ESP_LOGI(TAG, "Tentando inicializar MQTT...");
    // Tenta inicializar e conectar MQTT
    esp_err_t ret = mqtt_init();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "MQTT inicializado, iniciando cliente...");
        ret = mqtt_start();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "MQTT cliente iniciado com sucesso");
        } else {
            ESP_LOGW(TAG, "MQTT cliente falhou ao iniciar: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGW(TAG, "MQTT falhou ao inicializar: %s", esp_err_to_name(ret));
    }
    
    // Envia evento apropriado (sucesso ou erro)
    // NOTA: Erro não é crítico - sistema continua sem MQTT
    system_event_t event = (ret == ESP_OK) ? EVENT_MQTT_READY : EVENT_ERROR_OCCURRED;
    ESP_LOGI(TAG, "Enviando evento MQTT: %s", (ret == ESP_OK) ? "MQTT_READY" : "ERROR_OCCURRED");
    xQueueSend(event_queue, &event, portMAX_DELAY);
    
    ESP_LOGI(TAG, "MQTT Init Task finalizada");
    vTaskDelete(NULL);  // Auto-deleta após conclusão
}

/* ============================================================================
 * SEÇÃO 6: FUNÇÕES UTILITÁRIAS E MONITORAMENTO
 * ============================================================================
 * Funções auxiliares para consulta de estado, monitoramento de tasks
 * e gerenciamento de eventos do sistema
 */

// ──────────────────────────────────────────────────────────────────────────
// 6.1 CONSULTA DE ESTADO DO SISTEMA
// ──────────────────────────────────────────────────────────────────────────
/**
 * @brief Obtém o estado atual do sistema de forma thread-safe
 * 
 * Utiliza mutex para garantir leitura consistente do estado
 * mesmo quando outras tasks estão modificando-o
 * 
 * @return Estado atual ou STATE_ERROR se não conseguir lock do mutex
 */
static system_state_t get_system_state(void) {
    system_state_t state;
    if (xSemaphoreTake(state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        state = current_state;
        xSemaphoreGive(state_mutex);
    } else {
        state = STATE_ERROR;
    }
    return state;
}

// ──────────────────────────────────────────────────────────────────────────
// 6.2 VERIFICAÇÃO DE STATUS DE TASKS
// ──────────────────────────────────────────────────────────────────────────
/**
 * @brief Verifica se uma task está em execução
 * 
 * @param task_handle Handle da task a verificar
 * @return true se task está rodando, false se deletada/inválida/NULL
 */
static bool is_task_running(TaskHandle_t task_handle) {
    if (task_handle == NULL) return false;
    
    eTaskState task_state = eTaskGetState(task_handle);
    return (task_state != eDeleted && task_state != eInvalid);
}

// ──────────────────────────────────────────────────────────────────────────
// 6.3 LOG DE STATUS DAS TASKS (DIAGNÓSTICO)
// ──────────────────────────────────────────────────────────────────────────
/**
 * @brief Exibe log detalhado do status de todas as tasks do sistema
 * 
 * Útil para diagnóstico e monitoramento.
 * Chamado automaticamente a cada 30s em STATE_RUNNING
 * 
 * EXEMPLO DE OUTPUT:
 * ========== STATUS DAS TASKS ==========
 * Estado do Sistema: 6 (RUNNING)
 * Modbus Task: RODANDO
 * Sonda Control Task: RODANDO
 * MQTT Task: RODANDO
 * MQTT Status: CONECTADO
 * ======================================
 */
static void log_tasks_status(void) {
    ESP_LOGI(TAG, "========== STATUS DAS TASKS ==========");
    ESP_LOGI(TAG, "Estado do Sistema: %d", get_system_state());
    ESP_LOGI(TAG, "Modbus RTU Task: %s", 
             is_task_running(task_handles.modbus_rtu_task_handle) ? "RODANDO" : "PARADA");
    ESP_LOGI(TAG, "Modbus TCP Task: %s", 
             is_task_running(task_handles.modbus_tcp_task_handle) ? "RODANDO" : "PARADA");
    ESP_LOGI(TAG, "Sonda Control Task: %s", 
             is_task_running(task_handles.sonda_control_task_handle) ? "RODANDO" : "PARADA");
    ESP_LOGI(TAG, "WiFi Task: %s", 
             is_task_running(task_handles.wifi_task_handle) ? "RODANDO" : "PARADA");
    ESP_LOGI(TAG, "WebServer Task: %s", 
             is_task_running(task_handles.webserver_task_handle) ? "RODANDO" : "PARADA");
    ESP_LOGI(TAG, "MQTT Task: %s", 
             is_task_running(task_handles.mqtt_task_handle) ? "RODANDO" : "PARADA");
    ESP_LOGI(TAG, "MQTT Status: %s", 
             mqtt_is_connected() ? "CONECTADO" : "DESCONECTADO");
    ESP_LOGI(TAG, "State Machine Task: %s", 
             is_task_running(task_handles.state_machine_handle) ? "RODANDO" : "PARADA");
    ESP_LOGI(TAG, "=====================================");
}

// ──────────────────────────────────────────────────────────────────────────
// 6.4 API PÚBLICA: ENVIO DE EVENTOS PARA A MÁQUINA DE ESTADOS
// ──────────────────────────────────────────────────────────────────────────
#include "event_bus.h"              // API para outros módulos enviarem eventos
/**
 * @brief Envia um evento para a máquina de estados (API pública)
 * 
 * Pode ser chamada por outras partes do código para sinalizar eventos
 * como erros críticos, conclusão de operações, etc.
 * 
 * EXEMPLOS DE USO:
 * - send_system_event(EVENT_ERROR_OCCURRED); // Erro crítico
 * - send_system_event(EVENT_MQTT_READY);     // MQTT conectado
 * 
 * @param event Evento a enviar
 * @return ESP_OK se sucesso, ESP_ERR_INVALID_STATE se fila não existe,
 *         ESP_ERR_TIMEOUT se timeout (1s)
 */
esp_err_t send_system_event(system_event_t event) {
    if (event_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    BaseType_t result = xQueueSend(event_queue, &event, pdMS_TO_TICKS(1000));
    return (result == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

// Implementação do Event Bus público
esp_err_t eventbus_factory_reset_start(void) {
    return send_system_event(EVENT_FACTORY_RESET_START);
}

esp_err_t eventbus_factory_reset_complete(void) {
    return send_system_event(EVENT_FACTORY_RESET_COMPLETE);
}

// ──────────────────────────────────────────────────────────────────────────
// 6.5 TRANSIÇÃO DE ESTADO (THREAD-SAFE)
// ──────────────────────────────────────────────────────────────────────────
/**
 * @brief Faz transição entre estados de forma thread-safe
 * 
 * Utiliza mutex para garantir que transições sejam atômicas
 * Registra log de todas as transições para diagnóstico
 * RESETA contadores de timeout ao mudar de estado
 * 
 * @param new_state Novo estado para o qual transitar
 */
static void transition_to_state(system_state_t new_state) {
    if (xSemaphoreTake(state_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        ESP_LOGI(TAG, "Transição de estado: %d -> %d", current_state, new_state);
        
        // Reseta todos os contadores de timeout ao mudar de estado
        nvs_init_time_ms = 0;
        wifi_init_time_ms = 0;
        webserver_init_time_ms = 0;
        mqtt_init_time_ms = 0;
        tasks_start_time_ms = 0;
        
        current_state = new_state;
        last_state_tick = xTaskGetTickCount();  // Marca início do novo estado
        
        xSemaphoreGive(state_mutex);
    }
}

// ──────────────────────────────────────────────────────────────────────────
// 6.6 ATUALIZAÇÃO DE CONTADORES DE TIMEOUT
// ──────────────────────────────────────────────────────────────────────────
/**
 * @brief Atualiza contadores de tempo dos estados BUSY
 * 
 * Calcula tempo decorrido desde última verificação e atualiza
 * o contador correspondente ao estado atual.
 * Deve ser chamado em cada ciclo da máquina de estados.
 */
static void update_state_timers(void) {
    TickType_t current_tick = xTaskGetTickCount();
    uint32_t elapsed_ms = pdTICKS_TO_MS(current_tick - last_state_tick);
    last_state_tick = current_tick;
    
    // Atualiza contador do estado atual
    switch (current_state) {
        case STATE_NVS_SETUP:
            nvs_init_time_ms += elapsed_ms;
            break;
        case STATE_WIFI_INIT:
            wifi_init_time_ms += elapsed_ms;
            break;
        case STATE_WEBSERVER_START:
            webserver_init_time_ms += elapsed_ms;
            break;
        case STATE_MQTT_INIT:
            mqtt_init_time_ms += elapsed_ms;
            break;
        case STATE_TASKS_START:
            tasks_start_time_ms += elapsed_ms;
            break;
        case STATE_BUSY_FACTORY_RESET:
            factory_reset_time_ms += elapsed_ms;
            break;
        default:
            // Estados INIT, RUNNING e ERROR não têm timeout
            break;
    }
}

// ──────────────────────────────────────────────────────────────────────────
// 6.7 CARREGAMENTO DE CONFIGURAÇÕES DO main_config.json
// ──────────────────────────────────────────────────────────────────────────
/**
 * @brief Carrega flags de configuração do arquivo main_config.json
 * 
 * PROPÓSITO:
 * Esta função é o ponto central de controle do sistema baseado em flags.
 * Ela permite configurar quais recursos do sistema estarão ativos sem
 * necessidade de recompilar o firmware.
 * 
 * FUNCIONAMENTO DETALHADO:
 * 
 * 1. MONTAGEM SPIFFS:
 *    - Chama ensure_spiffs_mounted() para garantir acesso ao filesystem
 *    - Se falhar: Usa valores padrão e retorna ESP_FAIL
 * 
 * 2. VALORES PADRÃO (FALLBACK SEGURO):
 *    - Define FLAGS com valores seguros ANTES de tentar ler o arquivo
 *    - Garante que sistema seja acessível mesmo com arquivo ausente/corrompido
 *    - Padrões: AP=true (sistema acessível), STA/RTU/TCP=false (desabilitados)
 * 
 * 3. ABERTURA DE ARQUIVO:
 *    - Tenta: /spiffs/config/main_config.json (caminho recomendado)
 *    - Fallback: /spiffs/main_config.json (compatibilidade)
 *    - Se ambos falharem: Mantém valores padrão e retorna ESP_FAIL
 * 
 * 4. LEITURA E PARSING JSON:
 *    - Lê arquivo completo para buffer (máx 4KB)
 *    - Parseia com cJSON (biblioteca ESP-IDF)
 *    - Valida estrutura JSON
 *    - Se parsing falhar: Mantém valores padrão
 * 
 * 5. EXTRAÇÃO DE FLAGS:
 *    - Para cada flag (rtu_enabled, tcp_enabled, AP_enabled, sta_enabled):
 *      * Busca chave no JSON
 *      * Valida se é booleano
 *      * Atualiza FLAGS.xxx se válido
 *      * Mantém padrão se chave ausente ou inválida
 * 
 * 6. LOGGING DETALHADO:
 *    - Exibe caminho do arquivo tentado
 *    - Mostra se encontrou em cada caminho
 *    - Lista todas as flags carregadas com símbolos visuais (SIM/NAO)
 *    - Facilita debug de problemas de configuração
 * 
 * FORMATO DO ARQUIVO JSON:
 * {
 *   "rtu_enabled": false,    // Modbus RTU via Serial RS485
 *   "tcp_enabled": false,    // Modbus TCP via Ethernet/WiFi
 *   "AP_enabled": true,      // WiFi Access Point
 *   "sta_enabled": true,     // WiFi Station (conecta a roteador)
 *   "web_enabled": true      // Servidor Web (interface HTTP)
 * }
 * 
 * VALORES PADRÃO (FALLBACK SEGURO):
 * - rtu_enabled: false (Modbus RTU desabilitado)
 * - tcp_enabled: false (Modbus TCP desabilitado)
 * - AP_enabled: true   (AP sempre ativo - sistema acessível)
 * - sta_enabled: false (STA desabilitado por padrão)
 * 
 * INTEGRAÇÃO COM SISTEMA:
 * - Chamada em app_main() ANTES de qualquer inicialização de hardware
 * - FLAGS global é lida por:
 *   * wifi_init_task() → controla AP/STA
 *   * state_machine_task() → decide quais tasks criar
 *   * modbus tasks → decide se inicializa RTU/TCP
 * 
 * @return ESP_OK se arquivo lido com sucesso
 * @return ESP_FAIL se erro (valores padrão aplicados)
 */
static esp_err_t load_main_config_flags(void) {
    ESP_LOGI(TAG, "Carregando configurações de main_config.json...");
    
    // ─────────────────────────────────────────────────────────────────────
    // Garante que SPIFFS esteja montado (trata duplicação gracefully)
    // ─────────────────────────────────────────────────────────────────────
    esp_err_t mount_ret = ensure_spiffs_mounted();
    if (mount_ret != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS não pôde ser montado, usando valores padrão");
        FLAGS.rtu_enabled = false;
        FLAGS.tcp_enabled = false;
        FLAGS.AP_enabled = true;
        FLAGS.sta_enabled = false;
        FLAGS.web_enabled = true;
        FLAGS.log_main_flags = false;
        FLAGS.log_sonda_queue = false;
        FLAGS.log_sonda_values = false;
        FLAGS.log_modbus_tcp = false;
        return ESP_FAIL;
    }
    
    // ─────────────────────────────────────────────────────────────────────
    // Define valores padrão (fallback seguro)
    // ─────────────────────────────────────────────────────────────────────
    FLAGS.rtu_enabled = false;
    FLAGS.tcp_enabled = false;
    FLAGS.AP_enabled = true;   // AP sempre ativo por padrão (segurança)
    FLAGS.sta_enabled = false;
    FLAGS.web_enabled = true;  // Web habilitado por padrão para acesso à configuração
    
    // Valores padrão para flags de log (todas desabilitadas por padrão)
    FLAGS.log_main_flags = false;
    FLAGS.log_sonda_queue = false;
    FLAGS.log_sonda_values = false;
    FLAGS.log_modbus_tcp = false;
    
    // ─────────────────────────────────────────────────────────────────────
    // Abre arquivo do SPIFFS
    // ─────────────────────────────────────────────────────────────────────
    ESP_LOGI(TAG, "Tentando abrir: /spiffs/config/main_config.json");
    FILE *f = fopen("/spiffs/config/main_config.json", "r");
    if (f == NULL) {
        ESP_LOGW(TAG, "Arquivo main_config.json não encontrado em /spiffs/config/");
        ESP_LOGI(TAG, "   Tentando caminhos alternativos...");
        
        // Tenta caminho alternativo sem /config/
        f = fopen("/spiffs/main_config.json", "r");
        if (f == NULL) {
            ESP_LOGW(TAG, "Também não encontrado em /spiffs/main_config.json");
            ESP_LOGI(TAG, "   Usando valores padrão: AP_enabled=true, sta_enabled=false, rtu_enabled=false, tcp_enabled=false");
            return ESP_FAIL;
        } else {
            ESP_LOGI(TAG, "Arquivo encontrado em /spiffs/main_config.json");
        }
    } else {
        ESP_LOGI(TAG, "Arquivo encontrado em /spiffs/config/main_config.json");
    }
    
    // ─────────────────────────────────────────────────────────────────────
    // Lê conteúdo do arquivo
    // ─────────────────────────────────────────────────────────────────────
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (fsize <= 0 || fsize > 4096) {
        ESP_LOGE(TAG, "Tamanho do arquivo inválido: %ld bytes", fsize);
        fclose(f);
        return ESP_FAIL;
    }
    
    char *json_buffer = malloc(fsize + 1);
    if (json_buffer == NULL) {
        ESP_LOGE(TAG, "Falha ao alocar memória para JSON");
        fclose(f);
        return ESP_FAIL;
    }
    
    size_t read_size = fread(json_buffer, 1, fsize, f);
    json_buffer[read_size] = '\0';
    fclose(f);
    
    ESP_LOGD(TAG, "JSON lido (%zu bytes): %s", read_size, json_buffer);
    
    // ─────────────────────────────────────────────────────────────────────
    // Parseia JSON
    // ─────────────────────────────────────────────────────────────────────
    cJSON *root = cJSON_Parse(json_buffer);
    free(json_buffer);
    
    if (root == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        ESP_LOGE(TAG, "Erro ao parsear JSON: %s", error_ptr ? error_ptr : "desconhecido");
        return ESP_FAIL;
    }
    
    // ─────────────────────────────────────────────────────────────────────
    // Extrai valores do JSON (mantém padrão se chave não existir)
    // ─────────────────────────────────────────────────────────────────────
    cJSON *rtu = cJSON_GetObjectItem(root, "rtu_enabled");
    if (cJSON_IsBool(rtu)) {
        FLAGS.rtu_enabled = cJSON_IsTrue(rtu);
    }
    
    cJSON *tcp = cJSON_GetObjectItem(root, "tcp_enabled");
    if (cJSON_IsBool(tcp)) {
        FLAGS.tcp_enabled = cJSON_IsTrue(tcp);
    }
    
    cJSON *ap = cJSON_GetObjectItem(root, "AP_enabled");
    if (cJSON_IsBool(ap)) {
        FLAGS.AP_enabled = cJSON_IsTrue(ap);
    }
    
    cJSON *sta = cJSON_GetObjectItem(root, "sta_enabled");
    if (cJSON_IsBool(sta)) {
        FLAGS.sta_enabled = cJSON_IsTrue(sta);
    }
    cJSON *web = cJSON_GetObjectItem(root, "web_enabled");
    if (cJSON_IsBool(web)) {
        FLAGS.web_enabled = cJSON_IsTrue(web);
    }
    else { // Suporte legado: "webserver_enabled"
        cJSON *web_legacy = cJSON_GetObjectItem(root, "webserver_enabled");
        if (cJSON_IsBool(web_legacy)) {
            FLAGS.web_enabled = cJSON_IsTrue(web_legacy);
        }
    }
    
    // ─────────────────────────────────────────────────────────────────────
    // Extrai flags de controle de log (opcionais)
    // ─────────────────────────────────────────────────────────────────────
    cJSON *log_main_flags = cJSON_GetObjectItem(root, "log_main_flags");
    if (cJSON_IsBool(log_main_flags)) {
        FLAGS.log_main_flags = cJSON_IsTrue(log_main_flags);
    }
    
    cJSON *log_sonda_queue = cJSON_GetObjectItem(root, "log_sonda_queue");
    if (cJSON_IsBool(log_sonda_queue)) {
        FLAGS.log_sonda_queue = cJSON_IsTrue(log_sonda_queue);
    }
    
    cJSON *log_sonda_values = cJSON_GetObjectItem(root, "log_sonda_values");
    if (cJSON_IsBool(log_sonda_values)) {
        FLAGS.log_sonda_values = cJSON_IsTrue(log_sonda_values);
    }
    
    cJSON *log_modbus_tcp = cJSON_GetObjectItem(root, "log_modbus_tcp");
    if (cJSON_IsBool(log_modbus_tcp)) {
        FLAGS.log_modbus_tcp = cJSON_IsTrue(log_modbus_tcp);
    }
    
    cJSON_Delete(root);
    
    // ─────────────────────────────────────────────────────────────────────
    // Exibe configuração carregada
    // ─────────────────────────────────────────────────────────────────────
    ESP_LOGI(TAG, "Configurações carregadas com sucesso:");
    ESP_LOGI(TAG, "   ├─ AP_enabled:  %s", FLAGS.AP_enabled ? "SIM" : "NAO");
    ESP_LOGI(TAG, "   ├─ sta_enabled: %s", FLAGS.sta_enabled ? "SIM" : "NAO");
    ESP_LOGI(TAG, "   ├─ rtu_enabled: %s", FLAGS.rtu_enabled ? "SIM" : "NAO");
    ESP_LOGI(TAG, "   ├─ tcp_enabled: %s", FLAGS.tcp_enabled ? "SIM" : "NAO");
    ESP_LOGI(TAG, "   ├─ web_enabled: %s", FLAGS.web_enabled ? "SIM" : "NAO");
    ESP_LOGI(TAG, "   ├─ log_main_flags: %s", FLAGS.log_main_flags ? "SIM" : "NAO");
    ESP_LOGI(TAG, "   ├─ log_sonda_queue: %s", FLAGS.log_sonda_queue ? "SIM" : "NAO");
    ESP_LOGI(TAG, "   ├─ log_sonda_values: %s", FLAGS.log_sonda_values ? "SIM" : "NAO");
    ESP_LOGI(TAG, "   └─ log_modbus_tcp: %s", FLAGS.log_modbus_tcp ? "SIM" : "NAO");
    
    return ESP_OK;
}

// ──────────────────────────────────────────────────────────────────────────
// 6.8 CARREGAMENTO DE EVENTOS PENDENTES (OTIMIZAÇÃO)
// ──────────────────────────────────────────────────────────────────────────
/**
 * @brief Carrega TODOS os eventos pendentes da fila de uma vez
 * 
 * OTIMIZAÇÃO: Em vez de processar eventos um a um, esta função
 * coleta todos os eventos disponíveis e cria um bitmap de flags.
 * Isso permite processar múltiplos eventos simultaneamente e
 * priorizar eventos críticos (ERROR_OCCURRED)
 * 
 * FUNCIONAMENTO:
 * 1. Limpa processador de eventos anterior
 * 2. Coleta até 10 eventos da fila (sem bloqueio)
 * 3. Cria bitmaps de eventos normais e prioritários
 * 4. Armazena eventos em buffer para possível reprocessamento
 * 
 * ESTRUTURA DE DADOS:
 * - event_flags: Bitmap com todos os eventos pendentes
 * - priority_events: Bitmap apenas com eventos prioritários
 * - events_buffer: Array com eventos para referência
 * - event_count: Total de eventos coletados
 */
static void load_all_pending_events(void) {
    system_event_t event;
    
    // ─────────────────────────────────────────────────────────────────────
    // Limpa estrutura do processador
    // ─────────────────────────────────────────────────────────────────────
    event_processor.event_flags = 0;
    event_processor.priority_events = 0;
    event_processor.event_count = 0;
    
    // ─────────────────────────────────────────────────────────────────────
    // Coleta TODOS os eventos disponíveis (sem bloquear)
    // ─────────────────────────────────────────────────────────────────────
    while (xQueueReceive(event_queue, &event, 0) == pdTRUE && 
           event_processor.event_count < 10) {
        
        // Armazena evento no buffer
        event_processor.events_buffer[event_processor.event_count] = event;
        event_processor.event_count++;
        
        // Define flag do evento no bitmap
        uint32_t event_mask = (1 << event);
        event_processor.event_flags |= event_mask;
        
        // Marca se é evento prioritário
        if (event_mask & PRIORITY_EVENTS_MASK) {
            event_processor.priority_events |= event_mask;
        }
        
        ESP_LOGI(TAG, "Evento carregado: %d (total: %d)", event, event_processor.event_count);
    }
    
    // Exibe log apenas se habilitado via flag
    if (FLAGS.log_main_flags) {
        ESP_LOGI(TAG, "Flags carregadas: 0x%08lX, Prioritários: 0x%08lX, Total: %d", 
                 event_processor.event_flags, event_processor.priority_events, event_processor.event_count);
    }
}

// Task da máquina de estados principal
static void state_machine_task(void *param) {
    ESP_LOGI(TAG, "Máquina de Estados iniciada");
    
    // Inicializa timestamp para cálculo de timeouts
    last_state_tick = xTaskGetTickCount();
    
    while (1) {
        // ========== ATUALIZA CONTADORES DE TIMEOUT ==========
        update_state_timers();
        
        // ========== CARREGA TODAS AS FLAGS ANTES DO SWITCH CASE ==========
        load_all_pending_events();
        
        // Se não há eventos, aguarda um pouco e tenta novamente
        if (event_processor.event_count == 0) {
            // Aguarda por pelo menos um evento com timeout
            system_event_t single_event;
            if (xQueueReceive(event_queue, &single_event, pdMS_TO_TICKS(100)) == pdTRUE) {
                // Recoloca o evento na fila para processamento completo
                xQueueSendToFront(event_queue, &single_event, 0);
            }
            // Continua para verificar timeout mesmo sem eventos
        }
        
        if (event_processor.event_count > 0) {
            ESP_LOGI(TAG, "Processando %d eventos no estado: %d", event_processor.event_count, current_state);
        }
        
        // ========== PROCESSAMENTO COM TODAS AS FLAGS DISPONÍVEIS ==========
        switch (current_state) {
            case STATE_INIT:
                    // Verifica se há evento de inicialização completa
                    if (event_processor.event_flags & EVENT_MASK_INIT_COMPLETE) {
                        transition_to_state(STATE_NVS_SETUP);
                        // Cria task de validação do NVS
                        xTaskCreate(nvs_validation_task, "NVS Validation", 3072, NULL, 5, NULL);
                    }
                break;
                
            case STATE_NVS_SETUP:
                    // ========== VERIFICAÇÃO DE TIMEOUT ==========
                    if (nvs_init_time_ms > TIMEOUT_NVS_INIT_MS) {
                        ESP_LOGE(TAG, "TIMEOUT: NVS não inicializou em %lu ms", nvs_init_time_ms);
                        ESP_LOGE(TAG, "Sistema não pode continuar sem NVS funcional");
                        transition_to_state(STATE_ERROR);
                        break;
                    }
                    
                    // PRIORIDADE: Verifica erros primeiro
                    if (event_processor.priority_events & EVENT_MASK_ERROR_OCCURRED) {
                        ESP_LOGE(TAG, "Erro crítico na validação do NVS após %lu ms", nvs_init_time_ms);
                        ESP_LOGE(TAG, "Sistema não pode continuar sem NVS funcional");
                        transition_to_state(STATE_ERROR);
                    }
                    // Verifica se NVS foi validado com sucesso
                    else if (event_processor.event_flags & EVENT_MASK_NVS_READY) {
                        ESP_LOGI(TAG, "NVS validado em %lu ms", nvs_init_time_ms);
                        
                        // ========== VERIFICA SE WIFI ESTÁ HABILITADO ==========
                        if (!FLAGS.AP_enabled && !FLAGS.sta_enabled) {
                            ESP_LOGW(TAG, "WiFi desabilitado (AP=false, STA=false)");
                            if (FLAGS.web_enabled) {
                                ESP_LOGW(TAG, "WebServer NÃO será iniciado (sem rede)");
                            }
                            ESP_LOGI(TAG, " Pulando WiFi e WebServer, indo direto para MQTT");
                            
                            // Pula WiFi e WebServer, vai direto para MQTT
                            transition_to_state(STATE_MQTT_INIT);
                            xTaskCreate(mqtt_init_task, "MQTT Init", 4096, NULL, 5, NULL);
                        } else {
                            ESP_LOGI(TAG, "Prosseguindo para inicialização WiFi");
                            transition_to_state(STATE_WIFI_INIT);
                            // Cria task do WiFi
                            xTaskCreate(wifi_init_task, "WiFi Init", 8192, NULL, 5, &task_handles.wifi_task_handle);
                        }
                }
                break;
                
            case STATE_WIFI_INIT:
                    // ========== VERIFICAÇÃO DE TIMEOUT ==========
                    if (wifi_init_time_ms > TIMEOUT_WIFI_INIT_MS) {
                        ESP_LOGE(TAG, "TIMEOUT: WiFi não inicializou em %lu ms", wifi_init_time_ms);
                        ESP_LOGW(TAG, "Continuando sem WiFi STA - iniciando WebServer");
                        transition_to_state(STATE_WEBSERVER_START);
                        // Inicia WebServer mesmo sem WiFi STA (AP deve estar ativo)
                        if (FLAGS.web_enabled) {
                            xTaskCreate(webserver_init_task, "WebServer Init", 8192, NULL, 5, &task_handles.webserver_task_handle);
                        } else {
                            ESP_LOGI(TAG, " WebServer desabilitado via flag (web_enabled=false) - não será iniciado");
                        }
                        break;
                    }
                    
                    // PRIORIDADE: Verifica erros primeiro
                    if (event_processor.priority_events & EVENT_MASK_ERROR_OCCURRED) {
                        ESP_LOGE(TAG, "Erro na inicialização do WiFi após %lu ms", wifi_init_time_ms);
                        transition_to_state(STATE_ERROR);
                    }
                    // Depois verifica se WiFi está pronto
                    else if (event_processor.event_flags & EVENT_MASK_WIFI_READY) {
                        ESP_LOGI(TAG, "WiFi pronto em %lu ms - prosseguindo para WebServer", wifi_init_time_ms);
                        transition_to_state(STATE_WEBSERVER_START);
                        // Cria task de inicialização do WebServer
                        if (FLAGS.web_enabled) {
                            xTaskCreate(webserver_init_task, "WebServer Init", 8192, NULL, 5, &task_handles.webserver_task_handle);
                        } else {
                            ESP_LOGI(TAG, " WebServer desabilitado via flag (web_enabled=false) - não será iniciado");
                        }
                }
                break;
                
            case STATE_WEBSERVER_START:
                    // ========== VERIFICAÇÃO DE TIMEOUT ==========
                    if (webserver_init_time_ms > TIMEOUT_WEBSERVER_INIT_MS) {
                        ESP_LOGE(TAG, "TIMEOUT: WebServer não iniciou em %lu ms", webserver_init_time_ms);
                        ESP_LOGE(TAG, "Sistema não pode continuar sem interface web");
                        transition_to_state(STATE_ERROR);
                        break;
                    }
                    
                    // PRIORIDADE: Verifica erros primeiro
                    if (event_processor.priority_events & EVENT_MASK_ERROR_OCCURRED) {
                        ESP_LOGE(TAG, "Erro crítico na inicialização do WebServer após %lu ms", webserver_init_time_ms);
                        ESP_LOGE(TAG, "Sistema não pode continuar sem interface web");
                        transition_to_state(STATE_ERROR);
                    }
                    // Verifica se WebServer está pronto
                    else if (event_processor.event_flags & EVENT_MASK_WEBSERVER_READY) {
                        ESP_LOGI(TAG, "WebServer pronto em %lu ms - prosseguindo para MQTT", webserver_init_time_ms);
                        transition_to_state(STATE_MQTT_INIT);
                        // Cria task de inicialização do MQTT
                        xTaskCreate(mqtt_init_task, "MQTT Init", 4096, NULL, 5, NULL);
                }
                break;
                
            case STATE_MQTT_INIT:
                    ESP_LOGD(TAG, "Processando estado MQTT_INIT (tempo=%lu ms)", mqtt_init_time_ms);
                    
                    // ========== VERIFICAÇÃO DE TIMEOUT ==========
                    // MQTT não é crítico - timeout causa fallback
                    if (mqtt_init_time_ms > TIMEOUT_MQTT_INIT_MS) {
                        ESP_LOGW(TAG, "TIMEOUT: MQTT não conectou em %lu ms", mqtt_init_time_ms);
                        ESP_LOGW(TAG, "Continuando sem MQTT - sistema operará em modo offline");
                        ESP_LOGI(TAG, "Iniciando criação das tasks principais (MQTT timeout)...");
                        transition_to_state(STATE_TASKS_START);
                        
                        // ========== CRIA TASKS CONFORME CONFIGURAÇÃO ==========
                        ESP_LOGI(TAG, "Configuração Modbus: RTU=%s, TCP=%s",
                                 FLAGS.rtu_enabled ? "✓" : "✗",
                                 FLAGS.tcp_enabled ? "✓" : "✗");
                        
                        // Task da sonda (sempre ativa)
                        xTaskCreate(sonda_control_task, "Oxygen Sensor Task", 4096, NULL, 10, &task_handles.sonda_control_task_handle);
                        
                        // Modbus RTU (apenas se habilitado)
                        if (FLAGS.rtu_enabled) {
                            ESP_LOGI(TAG, "Iniciando Modbus RTU...");
                            xTaskCreate(modbus_slave_task, "Modbus RTU", 4096, NULL, 4, &task_handles.modbus_rtu_task_handle);
                            ESP_LOGI(TAG, "Modbus RTU task criada");
                        } else {
                            ESP_LOGI(TAG, " Modbus RTU desabilitado - pulando");
                        }
                        
                        // Modbus TCP (apenas se habilitado)
                        if (FLAGS.tcp_enabled) {
                            ESP_LOGI(TAG, "Inicializando Modbus TCP...");
                            esp_err_t tcp_ret1 = modbus_tcp_slave_init();
                            if (tcp_ret1 == ESP_OK) {
                                task_handles.modbus_tcp_task_handle = modbus_tcp_get_task_handle();
                                ESP_LOGI(TAG, "Modbus TCP inicializado com sucesso");
                            } else {
                                ESP_LOGW(TAG, "Modbus TCP falhou ao inicializar: %s", esp_err_to_name(tcp_ret1));
                            }
                        } else {
                            ESP_LOGI(TAG, " Modbus TCP desabilitado - pulando");
                        }
                        
                        // Envia evento de tasks prontas
                        system_event_t next_event = EVENT_TASKS_READY;
                        ESP_LOGI(TAG, "Enviando evento TASKS_READY");
                        xQueueSend(event_queue, &next_event, 0);
                        break;
                    }
                    
                    // MQTT pronto - cria todas as tasks
                    if (event_processor.event_flags & EVENT_MASK_MQTT_READY) {
                        ESP_LOGI(TAG, "MQTT inicializado em %lu ms", mqtt_init_time_ms);
                        ESP_LOGI(TAG, "Iniciando criação das tasks principais (com MQTT)...");
                        transition_to_state(STATE_TASKS_START);
                        
                        // ========== CRIA TASKS CONFORME CONFIGURAÇÃO ==========
                        ESP_LOGI(TAG, "Configuração Modbus: RTU=%s, TCP=%s",
                                 FLAGS.rtu_enabled ? "SIM" : "NAO",
                                 FLAGS.tcp_enabled ? "SIM" : "NAO");
                        
                        // Task da sonda (sempre ativa)
                        xTaskCreate(sonda_control_task, "Oxygen Sensor Task", 4096, NULL, 10, &task_handles.sonda_control_task_handle);
                        
                        // Task MQTT (já foi inicializado)
                        xTaskCreate(mqtt_client_task, "MQTT Client", 4096, NULL, 6, &task_handles.mqtt_task_handle);
                        
                        // Modbus RTU (apenas se habilitado)
                        if (FLAGS.rtu_enabled) {
                            ESP_LOGI(TAG, "Iniciando Modbus RTU...");
                            xTaskCreate(modbus_slave_task, "Modbus RTU", 4096, NULL, 4, &task_handles.modbus_rtu_task_handle);
                            ESP_LOGI(TAG, "Modbus RTU task criada");
                        } else {
                            ESP_LOGI(TAG, " Modbus RTU desabilitado - pulando");
                        }
                        
                        // Modbus TCP (apenas se habilitado)
                        if (FLAGS.tcp_enabled) {
                            ESP_LOGI(TAG, "Inicializando Modbus TCP...");
                            esp_err_t tcp_ret2 = modbus_tcp_slave_init();
                            if (tcp_ret2 == ESP_OK) {
                                task_handles.modbus_tcp_task_handle = modbus_tcp_get_task_handle();
                                ESP_LOGI(TAG, "Modbus TCP inicializado com sucesso");
                            } else {
                                ESP_LOGW(TAG, "Modbus TCP falhou ao inicializar: %s", esp_err_to_name(tcp_ret2));
                            }
                        } else {
                            ESP_LOGI(TAG, " Modbus TCP desabilitado - pulando");
                        }
                        
                        // Envia evento de tasks prontas
                        system_event_t next_event = EVENT_TASKS_READY;
                        ESP_LOGI(TAG, "Enviando evento TASKS_READY");
                        xQueueSend(event_queue, &next_event, 0);
                    }
                    // MQTT falhou - continua sem MQTT (sistema resiliente)
                    else if (event_processor.event_flags & EVENT_MASK_ERROR_OCCURRED) {
                        ESP_LOGW(TAG, "MQTT falhou após %lu ms, continuando sem MQTT", mqtt_init_time_ms);
                        ESP_LOGW(TAG, "Sistema operará em modo offline");
                        ESP_LOGI(TAG, "Iniciando criação das tasks principais (MQTT falhou)...");
                        transition_to_state(STATE_TASKS_START);
                        
                        // ========== CRIA TASKS CONFORME CONFIGURAÇÃO ==========
                        ESP_LOGI(TAG, "Configuração Modbus: RTU=%s, TCP=%s",
                                 FLAGS.rtu_enabled ? "✓" : "✗",
                                 FLAGS.tcp_enabled ? "✓" : "✗");
                        
                        // Task da sonda (sempre ativa)
                        xTaskCreate(sonda_control_task, "Oxygen Sensor Task", 4096, NULL, 10, &task_handles.sonda_control_task_handle);
                        
                        // Modbus RTU (apenas se habilitado)
                        if (FLAGS.rtu_enabled) {
                            ESP_LOGI(TAG, "Iniciando Modbus RTU...");
                            xTaskCreate(modbus_slave_task, "Modbus RTU", 4096, NULL, 4, &task_handles.modbus_rtu_task_handle);
                            ESP_LOGI(TAG, "Modbus RTU task criada");
                        } else {
                            ESP_LOGI(TAG, " Modbus RTU desabilitado - pulando");
                        }
                        
                        // Modbus TCP (apenas se habilitado)
                        if (FLAGS.tcp_enabled) {
                            ESP_LOGI(TAG, "Inicializando Modbus TCP...");
                            esp_err_t tcp_ret3 = modbus_tcp_slave_init();
                            if (tcp_ret3 == ESP_OK) {
                                task_handles.modbus_tcp_task_handle = modbus_tcp_get_task_handle();
                                ESP_LOGI(TAG, "Modbus TCP inicializado com sucesso");
                            } else {
                                ESP_LOGW(TAG, "Modbus TCP falhou ao inicializar: %s", esp_err_to_name(tcp_ret3));
                            }
                        } else {
                            ESP_LOGI(TAG, " Modbus TCP desabilitado - pulando");
                        }
                        
                        // Envia evento de tasks prontas
                        system_event_t next_event = EVENT_TASKS_READY;
                        ESP_LOGI(TAG, "Enviando evento TASKS_READY");
                        xQueueSend(event_queue, &next_event, 0);
                }
                break;
                
            case STATE_TASKS_START:
                    // ========== VERIFICAÇÃO DE TIMEOUT ==========
                    if (tasks_start_time_ms > TIMEOUT_TASKS_START_MS) {
                        ESP_LOGE(TAG, "TIMEOUT: Tasks não iniciaram em %lu ms", tasks_start_time_ms);
                        transition_to_state(STATE_ERROR);
                        break;
                    }
                    
                    // Verifica se tasks estão prontas
                    if (event_processor.event_flags & EVENT_MASK_TASKS_READY) {
                        ESP_LOGI(TAG, "Sistema totalmente inicializado em %lu ms", tasks_start_time_ms);
                        transition_to_state(STATE_RUNNING);
                        ESP_LOGI(TAG, "========== SISTEMA OPERACIONAL ==========");
                        
                        // Reseta contador de erros - sistema inicializou com sucesso
                        error_recovery_count = 0;
                }
                break;
                
            case STATE_RUNNING:
                    // Estado operacional - pode processar múltiplos eventos aqui
                    ESP_LOGD(TAG, "Sistema rodando normalmente com %d eventos", event_processor.event_count);
                    
                    // Início de Factory Reset: transita para estado BUSY
                    if (event_processor.event_flags & EVENT_MASK_FACTORY_RESET_START) {
                        ESP_LOGW(TAG, "Iniciando Factory Reset - sistema entrará em modo ocupado");
                        transition_to_state(STATE_BUSY_FACTORY_RESET);
                        break;
                    }
                    
                    // ========== FUTURO: PROCESSAMENTO DE EVENTOS NO ESTADO RUNNING ==========
                    // Exemplo para Modbus Gateway e outras funções futuras:
                    /*
                    if (event_processor.event_flags & EVENT_MASK_MODBUS_GATEWAY_REQUEST) {
                        ESP_LOGI(TAG, "Processando requisição Modbus Gateway");
                        // process_modbus_gateway_request();
                    }
                    
                    if (event_processor.event_flags & EVENT_MASK_CONFIG_UPDATE) {
                        ESP_LOGI(TAG, "Atualizando configurações");
                        // update_system_config();
                    }
                    
                    if (event_processor.event_flags & EVENT_MASK_DIAGNOSTIC_REQUEST) {
                        ESP_LOGI(TAG, "Executando diagnósticos");
                        // run_system_diagnostics();
                    }
                    */
                    
                    // A cada 30 segundos, loga o status das tasks (opcional)
                    static uint32_t last_status_log = 0;
                    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
                    if (current_time - last_status_log > 30000) {
                        log_tasks_status();
                        last_status_log = current_time;
                    }
                break;
                
            case STATE_BUSY_FACTORY_RESET:
                    // Verifica timeout do reset de fábrica
                    if (factory_reset_time_ms > TIMEOUT_FACTORY_RESET_MS) {
                        ESP_LOGE(TAG, "TIMEOUT: Factory Reset não concluiu em %lu ms", factory_reset_time_ms);
                        ESP_LOGE(TAG, "Reiniciando ESP32 para concluir reset...");
                        vTaskDelay(pdMS_TO_TICKS(500));
                        esp_restart();
                    }

                    // Aguarda evento de conclusão do reset
                    if (event_processor.event_flags & EVENT_MASK_FACTORY_RESET_COMPLETE) {
                        ESP_LOGI(TAG, "Factory Reset concluído em %lu ms - reiniciando ESP32", factory_reset_time_ms);
                        vTaskDelay(pdMS_TO_TICKS(500));
                        esp_restart();
                    }

                    // Enquanto ocupado, ignora outros eventos
                break;

            case STATE_ERROR:
                    // ========== RECUPERAÇÃO AUTOMÁTICA DE ERRO ==========
                    error_recovery_count++;
                    
                    ESP_LOGE(TAG, "Sistema em estado de ERRO (tentativa #%d de %d)", 
                             error_recovery_count, MAX_RECOVERY_ATTEMPTS);
                    
                    // Verifica se ainda pode tentar recuperar
                    if (error_recovery_count < MAX_RECOVERY_ATTEMPTS) {
                        ESP_LOGW(TAG, "Tentando recuperação automática do sistema...");
                        ESP_LOGI(TAG, "⏳ Aguardando 5 segundos antes de reiniciar inicialização...");
                        
                        vTaskDelay(pdMS_TO_TICKS(5000));  // Aguarda 5s
                        
                        // Reinicia sequência de inicialização
                        ESP_LOGI(TAG, "Reiniciando sequência de inicialização...");
                        transition_to_state(STATE_INIT);
                        
                        // Envia evento para começar de novo
                        system_event_t event = EVENT_INIT_COMPLETE;
                        xQueueSend(event_queue, &event, 0);
                    } 
                    else {
                        // Esgotou tentativas de recuperação - reinicia ESP32
                        ESP_LOGE(TAG, "FALHA: Recuperação automática falhou após %d tentativas", 
                                 MAX_RECOVERY_ATTEMPTS);
                        ESP_LOGE(TAG, "REINICIANDO ESP32 em 3 segundos...");
                        ESP_LOGE(TAG, "═══════════════════════════════════════════════════");
                        
                        vTaskDelay(pdMS_TO_TICKS(3000));  // Aguarda 3s para log ser exibido
                        
                        // Reinicia o ESP32 completamente
                        esp_restart();
                    }
                break;
                
            default:
                    ESP_LOGW(TAG, "Estado desconhecido: %d", current_state);
                break;
        }
        
        // Pequeno delay para não sobrecarregar o sistema
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Função para fazer o reset de fábrica (igual à do webserver)
// NOTA: Função comentada temporariamente pois não está sendo usada
/*
static void factory_reset() {
    ESP_LOGI(TAG, "Reset de fábrica acionado pelo botão físico!");
    
    // Sinaliza para o frontend que o reset está pendente
    reset_pending = true;
    
    // Aguarda alguns segundos para o frontend exibir a mensagem
    ESP_LOGI(TAG, "Aguardando 4 segundos para o frontend exibir a mensagem...");
    vTaskDelay(pdMS_TO_TICKS(4000)); // 4 segundos
    
    // Aguarda um pouco antes de executar o reset
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Apaga NVS
    esp_err_t ret = nvs_flash_erase();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao apagar NVS: %s", esp_err_to_name(ret));
    }
    
    // Remove arquivos com proteção
    if (remove("/spiffs/conteudo.json") != 0) {
        ESP_LOGW(TAG, "Arquivo conteudo.json não encontrado ou já removido");
    }
    if (remove("/spiffs/config.json") != 0) {
        ESP_LOGW(TAG, "Arquivo config.json não encontrado ou já removido");
    }
    if (remove("/data/config/network_config.json") != 0) {
    }
    
    ESP_LOGI(TAG, "Reiniciando ESP32...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}
*/

void app_main(void)
{
    ESP_LOGI(TAG, "========== INICIANDO SISTEMA COM MÁQUINA DE ESTADOS ==========");

    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    // ========== INICIALIZAÇÃO DA MÁQUINA DE ESTADOS ==========
    
    // 1) Inicializa componentes básicos
    esp_log_level_set("*", ESP_LOG_INFO);
    
    // DEBUG: Habilita logs detalhados para sistema de filas e main
    esp_log_level_set("QUEUE_MANAGER", ESP_LOG_DEBUG);
    esp_log_level_set("MODBUS_SLAVE", ESP_LOG_DEBUG);
    esp_log_level_set("MODBUS_TCP", ESP_LOG_DEBUG);    // Adicionando logs TCP
    esp_log_level_set("MAIN", ESP_LOG_DEBUG);          // Adicionando logs MAIN
    esp_log_level_set("SONDA_CONTROL", ESP_LOG_INFO);
    
    // 2) Inicializa NVS
    ESP_LOGI(TAG, "Inicializando NVS...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    ESP_LOGI(TAG, "NVS inicializado com sucesso");
    
    // ========== CARREGAMENTO DAS FLAGS DE CONFIGURAÇÃO ==========
    ESP_LOGI(TAG, "Carregando flags de configuração do main_config.json...");
    esp_err_t config_ret = load_main_config_flags();
    if (config_ret != ESP_OK) {
        ESP_LOGW(TAG, "Usando configuração padrão (AP ativo, outros recursos desabilitados)");
    }
    
    // 3) Cria componentes da máquina de estados
    event_queue = xQueueCreate(10, sizeof(system_event_t));
    if (event_queue == NULL) {
        ESP_LOGE(TAG, "Falha ao criar fila de eventos");
        return;
    }
    
    state_mutex = xSemaphoreCreateMutex();
    if (state_mutex == NULL) {
        ESP_LOGE(TAG, "Falha ao criar mutex de estado");
        return;
    }
    
    // ========== NOVO: INICIALIZAÇÃO DO SISTEMA DE FILAS ==========
    // Inicializa todas as filas para comunicação inter-tasks
    ESP_LOGI(TAG, "Inicializando sistema de filas...");
    esp_err_t queue_ret = queue_manager_init();
    if (queue_ret != ESP_OK) {
        ESP_LOGE(TAG, "ERRO: Falha ao inicializar sistema de filas!");
        return;
    }
    ESP_LOGI(TAG, "Sistema de filas inicializado com sucesso");
    
    // 4) Inicializa estado inicial
    current_state = STATE_INIT;
    memset(&task_handles, 0, sizeof(task_handles_t));
    
    // 5) Cria task da máquina de estados
    BaseType_t task_created = xTaskCreate(
        state_machine_task,
        "State Machine", 
        4096, 
        NULL, 
        configMAX_PRIORITIES - 1,  // Alta prioridade para a máquina de estados
        &task_handles.state_machine_handle
    );
    
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Falha ao criar task da máquina de estados");
        return;
    }
    
    // 6) Inicia a máquina de estados enviando o primeiro evento
    system_event_t init_event = EVENT_INIT_COMPLETE;
    if (xQueueSend(event_queue, &init_event, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Falha ao enviar evento inicial");
        return;
    }
    
    ESP_LOGI(TAG, "Máquina de Estados configurada e iniciada");
    ESP_LOGI(TAG, "========== SISTEMA DELEGADO À MÁQUINA DE ESTADOS ==========");
    
    // A partir daqui, toda a inicialização é controlada pela máquina de estados
    // A função app_main() termina, mas as tasks continuam rodando
}