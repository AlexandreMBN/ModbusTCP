/*
 * ========================================================================
 * MAIN.C - GERENCIADOR DE TASKS COM MÁQUINA DE ESTADOS
 * ========================================================================
 * 
 * Este arquivo implementa uma máquina de estados para gerenciar todas as 
 * tasks do sistema de forma organizada e controlada.
 * 
 * TASKS GERENCIADAS:
 * - Modbus Task (modbus_slave_task) - Prioridade: 3
 * - Sonda Control Task (sonda_control_task) - Prioridade: 10  
 * - WiFi Init Task (wrapper) - Prioridade: 5
 * - WebServer Init Task (wrapper) - Prioridade: 5
 * - MQTT Client Task - Prioridade: 6
 * - State Machine Task - Prioridade: Máxima
 * 
 * ESTADOS DO SISTEMA:
 * - STATE_INIT: Inicialização
 * - STATE_NVS_SETUP: Configuração do NVS
 * - STATE_WIFI_INIT: Inicialização do WiFi
 * - STATE_WEBSERVER_START: Inicialização do WebServer
 * - STATE_MQTT_INIT: Inicialização do cliente MQTT
 * - STATE_TASKS_START: Criação das tasks principais
 * - STATE_RUNNING: Sistema operacional
 * - STATE_ERROR: Estado de erro
 * 
 * EVENTOS:
 * - EVENT_INIT_COMPLETE: Inicialização completa
 * - EVENT_NVS_READY: NVS pronto
 * - EVENT_WIFI_READY: WiFi pronto
 * - EVENT_WEBSERVER_READY: WebServer pronto
 * - EVENT_MQTT_READY: Cliente MQTT pronto
 * - EVENT_TASKS_READY: Tasks principais prontas
 * - EVENT_ERROR_OCCURRED: Erro ocorreu
 * 
 * ========================================================================
 */

#include <stdio.h>
#include "mbcontroller.h"
#include "modbus_slave_task.h"
#include "modbus_task.h"
#include "other_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "FreeRTOSConfig.h"

#include <nvs_flash.h>
#include <esp_log.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <stdbool.h>
#include "wifi_manager.h"
#include "webserver.h"
#include "oxygen_sensor_task.h"
#include "mqtt_client_task.h"

#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// Declarações das funções de restauração do NVS
void load_coils_from_nvs(void);
void load_holding_regs_from_nvs(void);

static const char *TAG = "MAIN";

// ========== MÁQUINA DE ESTADOS PARA GERENCIAMENTO DE TASKS ==========

// Definição dos estados do sistema
typedef enum {
    STATE_INIT,
    STATE_NVS_SETUP,
    STATE_WIFI_INIT,
    STATE_WEBSERVER_START,
    STATE_MQTT_INIT,
    STATE_TASKS_START,
    STATE_RUNNING,
    STATE_ERROR
} system_state_t;

// Definição dos eventos do sistema
typedef enum {
    EVENT_INIT_COMPLETE,
    EVENT_NVS_READY,
    EVENT_WIFI_READY,
    EVENT_WEBSERVER_READY,
    EVENT_MQTT_READY,
    EVENT_TASKS_READY,
    EVENT_ERROR_OCCURRED
} system_event_t;

// Estrutura para controle das tasks
typedef struct {
    TaskHandle_t modbus_task_handle;
    TaskHandle_t sonda_control_task_handle;
    TaskHandle_t wifi_task_handle;
    TaskHandle_t webserver_task_handle;
    TaskHandle_t mqtt_task_handle;
    TaskHandle_t state_machine_handle;
} task_handles_t;

// Variáveis globais para a máquina de estados
static system_state_t current_state = STATE_INIT;
static QueueHandle_t event_queue;
static task_handles_t task_handles = {0};
static SemaphoreHandle_t state_mutex;

// Variável global para sinalizar reset pendente
volatile bool reset_pending = false;

// Definição do pino do botão de reset
#define RESET_BUTTON_GPIO GPIO_NUM_4
#define RESET_BUTTON_PRESS_TIME_MS 3000  // Tempo que o botão deve ficar pressionado (3 segundos)

// Definição do pino do LED de feedback
#define RESET_LED_GPIO GPIO_NUM_2

// ========== WRAPPER FUNCTIONS PARA TRANSFORMAR FUNÇÕES EM TASKS ==========

// Task wrapper para inicialização do WiFi
static void wifi_init_task(void *param) {
    ESP_LOGI(TAG, "WiFi Init Task iniciada");
    
    // Inicia WiFi AP e WebServer usando o wifi_manager
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI(TAG, "Chamando start_wifi_ap()...");
    
    start_wifi_ap();
    
    ESP_LOGI(TAG, "start_wifi_ap() retornou");
    
    // Aguarda o WiFi/AP inicializar (timeout 10s)
    ESP_LOGI(TAG, "Aguardando WiFi/AP inicializar...");
    int wait_ms = 0;
    const int timeout_ms = 10000;
    bool wifi_ready = false;
    
    while (wait_ms < timeout_ms) {
        if (wifi_is_initialized()) {
            wifi_status_t st = wifi_get_status();
            if (st.ap_active) {
                ESP_LOGI(TAG, "AP ativo detectado");
                wifi_ready = true;
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        wait_ms += 200;
    }
    
    // Envia evento para a máquina de estados
    system_event_t event = wifi_ready ? EVENT_WIFI_READY : EVENT_ERROR_OCCURRED;
    xQueueSend(event_queue, &event, portMAX_DELAY);
    
    ESP_LOGI(TAG, "WiFi Init Task finalizada");
    vTaskDelete(NULL);
}

// Task wrapper para inicialização do WebServer
static void webserver_init_task(void *param) {
    ESP_LOGI(TAG, "WebServer Init Task iniciada");
    
    ESP_LOGI(TAG, "Chamando start_web_server()...");
    start_web_server();
    ESP_LOGI(TAG, "start_web_server() retornou");
    
    // Envia evento para a máquina de estados
    system_event_t event = EVENT_WEBSERVER_READY;
    xQueueSend(event_queue, &event, portMAX_DELAY);
    
    ESP_LOGI(TAG, "WebServer Init Task finalizada");
    vTaskDelete(NULL);
}

// Task wrapper para inicialização do MQTT
static void mqtt_init_task(void *param) {
    ESP_LOGI(TAG, "MQTT Init Task iniciada");
    
    // Aguarda um pouco para garantir que WiFi esteja estável
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Inicializa MQTT
    esp_err_t ret = mqtt_init();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "MQTT inicializado, iniciando cliente...");
        ret = mqtt_start();
    }
    
    // Envia evento para a máquina de estados
    system_event_t event = (ret == ESP_OK) ? EVENT_MQTT_READY : EVENT_ERROR_OCCURRED;
    xQueueSend(event_queue, &event, portMAX_DELAY);
    
    ESP_LOGI(TAG, "MQTT Init Task finalizada");
    vTaskDelete(NULL);
}

// ========== FUNÇÕES DE MONITORAMENTO E CONTROLE ==========

// Função para obter status do sistema
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

// Função para verificar se uma task está rodando
static bool is_task_running(TaskHandle_t task_handle) {
    if (task_handle == NULL) return false;
    
    eTaskState task_state = eTaskGetState(task_handle);
    return (task_state != eDeleted && task_state != eInvalid);
}

// Função para obter informações das tasks
static void log_tasks_status(void) {
    ESP_LOGI(TAG, "========== STATUS DAS TASKS ==========");
    ESP_LOGI(TAG, "Estado do Sistema: %d", get_system_state());
    ESP_LOGI(TAG, "Modbus Task: %s", is_task_running(task_handles.modbus_task_handle) ? "RODANDO" : "PARADA");
    ESP_LOGI(TAG, "Sonda Control Task: %s", is_task_running(task_handles.sonda_control_task_handle) ? "RODANDO" : "PARADA");
    ESP_LOGI(TAG, "WiFi Task: %s", is_task_running(task_handles.wifi_task_handle) ? "RODANDO" : "PARADA");
    ESP_LOGI(TAG, "WebServer Task: %s", is_task_running(task_handles.webserver_task_handle) ? "RODANDO" : "PARADA");
    ESP_LOGI(TAG, "MQTT Task: %s", is_task_running(task_handles.mqtt_task_handle) ? "RODANDO" : "PARADA");
    ESP_LOGI(TAG, "MQTT Status: %s", mqtt_is_connected() ? "CONECTADO" : "DESCONECTADO");
    ESP_LOGI(TAG, "State Machine Task: %s", is_task_running(task_handles.state_machine_handle) ? "RODANDO" : "PARADA");
    ESP_LOGI(TAG, "=====================================");
}

// Função para enviar eventos para a máquina de estados (pode ser usada por outras partes do código)
esp_err_t send_system_event(system_event_t event) {
    if (event_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    BaseType_t result = xQueueSend(event_queue, &event, pdMS_TO_TICKS(1000));
    return (result == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

// ========== MÁQUINA DE ESTADOS ==========

// Função para transição de estado
static void transition_to_state(system_state_t new_state) {
    if (xSemaphoreTake(state_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        ESP_LOGI(TAG, "Transição de estado: %d -> %d", current_state, new_state);
        current_state = new_state;
        xSemaphoreGive(state_mutex);
    }
}

// ========== ESTRUTURA PARA MÚLTIPLOS EVENTOS ==========
typedef struct {
    uint32_t event_flags;           // Bitmap das flags de eventos
    uint32_t priority_events;       // Bitmap dos eventos prioritários
    system_event_t events_buffer[10]; // Buffer para eventos pendentes
    uint8_t event_count;            // Contador de eventos
} event_processor_t;

// Bitmap masks para eventos
#define EVENT_MASK_INIT_COMPLETE    (1 << EVENT_INIT_COMPLETE)
#define EVENT_MASK_NVS_READY        (1 << EVENT_NVS_READY)
#define EVENT_MASK_WIFI_READY       (1 << EVENT_WIFI_READY)
#define EVENT_MASK_WEBSERVER_READY  (1 << EVENT_WEBSERVER_READY)
#define EVENT_MASK_MQTT_READY       (1 << EVENT_MQTT_READY)
#define EVENT_MASK_TASKS_READY      (1 << EVENT_TASKS_READY)
#define EVENT_MASK_ERROR_OCCURRED   (1 << EVENT_ERROR_OCCURRED)

// Eventos prioritários (processados primeiro)
#define PRIORITY_EVENTS_MASK (EVENT_MASK_ERROR_OCCURRED)

static event_processor_t event_processor = {0};

// Função para carregar todas as flags disponíveis
static void load_all_pending_events(void) {
    system_event_t event;
    
    // Reset do processador de eventos
    event_processor.event_flags = 0;
    event_processor.priority_events = 0;
    event_processor.event_count = 0;
    
    // Coleta TODOS os eventos disponíveis na fila (sem timeout)
    while (xQueueReceive(event_queue, &event, 0) == pdTRUE && 
           event_processor.event_count < 10) {
        
        // Armazena evento no buffer
        event_processor.events_buffer[event_processor.event_count] = event;
        event_processor.event_count++;
        
        // Define flag do evento
        uint32_t event_mask = (1 << event);
        event_processor.event_flags |= event_mask;
        
        // Marca se é evento prioritário
        if (event_mask & PRIORITY_EVENTS_MASK) {
            event_processor.priority_events |= event_mask;
        }
        
        ESP_LOGI(TAG, "Evento carregado: %d (total: %d)", event, event_processor.event_count);
    }
    
    ESP_LOGI(TAG, "Flags carregadas: 0x%08lX, Prioritários: 0x%08lX, Total: %d", 
             event_processor.event_flags, event_processor.priority_events, event_processor.event_count);
}

// Task da máquina de estados principal
static void state_machine_task(void *param) {
    ESP_LOGI(TAG, "Máquina de Estados iniciada");
    
    while (1) {
        // ========== CARREGA TODAS AS FLAGS ANTES DO SWITCH CASE ==========
        load_all_pending_events();
        
        // Se não há eventos, aguarda um pouco e tenta novamente
        if (event_processor.event_count == 0) {
            // Aguarda por pelo menos um evento com timeout
            system_event_t single_event;
            if (xQueueReceive(event_queue, &single_event, pdMS_TO_TICKS(1000)) == pdTRUE) {
                // Recoloca o evento na fila para processamento completo
                xQueueSendToFront(event_queue, &single_event, 0);
            }
            continue;
        }
        
        ESP_LOGI(TAG, "Processando %d eventos no estado: %d", event_processor.event_count, current_state);
        
        // ========== PROCESSAMENTO COM TODAS AS FLAGS DISPONÍVEIS ==========
        switch (current_state) {
            case STATE_INIT:
                    // Verifica se há evento de inicialização completa
                    if (event_processor.event_flags & EVENT_MASK_INIT_COMPLETE) {
                        transition_to_state(STATE_NVS_SETUP);
                        // Envia próximo evento
                        system_event_t next_event = EVENT_NVS_READY;
                        xQueueSend(event_queue, &next_event, 0);
                    }
                break;
                
            case STATE_NVS_SETUP:
                    // Verifica se há evento NVS pronto
                    if (event_processor.event_flags & EVENT_MASK_NVS_READY) {
                        transition_to_state(STATE_WIFI_INIT);
                        // Cria task do WiFi
                        xTaskCreate(wifi_init_task, "WiFi Init", 8192, NULL, 5, &task_handles.wifi_task_handle);
                }
                break;
                
            case STATE_WIFI_INIT:
                    // PRIORIDADE: Verifica erros primeiro
                    if (event_processor.priority_events & EVENT_MASK_ERROR_OCCURRED) {
                        transition_to_state(STATE_ERROR);
                        ESP_LOGE(TAG, "Erro na inicialização do WiFi");
                    }
                    // Depois verifica se WiFi está pronto
                    else if (event_processor.event_flags & EVENT_MASK_WIFI_READY) {
                        transition_to_state(STATE_WEBSERVER_START);
                        // Cria task do WebServer
                        xTaskCreate(webserver_init_task, "WebServer Init", 8192, NULL, 5, &task_handles.webserver_task_handle);
                }
                break;
                
            case STATE_WEBSERVER_START:
                    // Verifica se WebServer está pronto
                    if (event_processor.event_flags & EVENT_MASK_WEBSERVER_READY) {
                        transition_to_state(STATE_MQTT_INIT);
                        // Cria task de inicialização do MQTT
                        xTaskCreate(mqtt_init_task, "MQTT Init", 4096, NULL, 5, NULL);
                }
                break;
                
            case STATE_MQTT_INIT:
                    // MQTT pronto - cria todas as tasks
                    if (event_processor.event_flags & EVENT_MASK_MQTT_READY) {
                        ESP_LOGI(TAG, "MQTT inicializado com sucesso");
                        transition_to_state(STATE_TASKS_START);
                        
                        // Cria tasks principais
                        xTaskCreate(modbus_slave_task, "Modbus Task", 4096, NULL, 3, &task_handles.modbus_task_handle);
                        xTaskCreate(sonda_control_task, "Oxygen Sensor Task", 4096, NULL, 10, &task_handles.sonda_control_task_handle);
                        
                        // Cria task do cliente MQTT (apenas se MQTT funcionou)
                        xTaskCreate(mqtt_client_task, "MQTT Client", 4096, NULL, 6, &task_handles.mqtt_task_handle);
                        
                        // Envia evento de tasks prontas
                        system_event_t next_event = EVENT_TASKS_READY;
                        xQueueSend(event_queue, &next_event, 0);
                    }
                    // MQTT falhou - continua sem MQTT (sistema resiliente)
                    else if (event_processor.event_flags & EVENT_MASK_ERROR_OCCURRED) {
                        ESP_LOGW(TAG, "MQTT falhou, continuando sem MQTT - sistema operará em modo offline");
                        transition_to_state(STATE_TASKS_START);
                        
                        // Cria apenas tasks essenciais (sem MQTT)
                        xTaskCreate(modbus_slave_task, "Modbus Task", 4096, NULL, 3, &task_handles.modbus_task_handle);
                        xTaskCreate(sonda_control_task, "Oxygen Sensor Task", 4096, NULL, 10, &task_handles.sonda_control_task_handle);
                        
                        // Envia evento de tasks prontas
                        system_event_t next_event = EVENT_TASKS_READY;
                        xQueueSend(event_queue, &next_event, 0);
                }
                break;
                
            case STATE_TASKS_START:
                    // Verifica se tasks estão prontas
                    if (event_processor.event_flags & EVENT_MASK_TASKS_READY) {
                        transition_to_state(STATE_RUNNING);
                        ESP_LOGI(TAG, "Sistema totalmente inicializado e rodando");
                }
                break;
                
            case STATE_RUNNING:
                    // Estado operacional - pode processar múltiplos eventos aqui
                    ESP_LOGD(TAG, "Sistema rodando normalmente com %d eventos", event_processor.event_count);
                    
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
                
            case STATE_ERROR:
                    ESP_LOGE(TAG, "Sistema em estado de erro");
                    vTaskDelay(pdMS_TO_TICKS(5000)); // Aguarda antes de tentar recuperar
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
    if (remove("/spiffs/network_config.json") != 0) {
    }
    
    ESP_LOGI(TAG, "Reiniciando ESP32...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

void app_main(void)
{
    ESP_LOGI(TAG, "========== INICIANDO SISTEMA COM MÁQUINA DE ESTADOS ==========");

    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    // ========== INICIALIZAÇÃO DA MÁQUINA DE ESTADOS ==========
    
    // 1) Inicializa componentes básicos
    esp_log_level_set("*", ESP_LOG_INFO);
    
    // 2) Inicializa NVS
    ESP_LOGI(TAG, "Inicializando NVS...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    ESP_LOGI(TAG, "NVS inicializado com sucesso");
    
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