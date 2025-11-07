/**
 * @file modbus_manager.c
 * @brief Implementação do sistema de gerenciamento e alternância Modbus RTU/TCP
 * 
 * Este módulo implementa a lógica de alternância dinâmica entre Modbus RTU e TCP,
 * mantendo sincronização de registradores e fornecendo uma interface unificada.
 * 
 * FUNCIONAMENTO:
 * -------------
 * 1. Task principal monitora configuração de modo
 * 2. Detecta mudanças e executa transições
 * 3. Mantém registradores sincronizados entre implementações
 * 4. Fornece fallback automático RTU quando WiFi falha
 * 
 * MÁQUINA DE ESTADOS:
 * ------------------
 * IDLE → RTU/TCP → SWITCHING → RTU/TCP → IDLE
 *   ↓                                      ↑
 * ERROR ←────────────────────────────────────
 * 
 * @author Sistema ESP32  
 * @date 2025
 */

#include "modbus_manager.h"
#include "modbus_params.h"       // Registradores compartilhados
#include "modbus_config.h"       // Configurações Modbus
#include "modbus_slave_task.h"   // Task RTU original
#include "modbus_register_sync.h" // Funções de sincronização
#include "wifi_manager.h"        // Status WiFi
#include "config_manager.h"      // Leitura/escrita config.json

#include "esp_log.h"
#include "esp_netif.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * CONSTANTES E DEFINIÇÕES INTERNAS
 * ============================================================================ */

static const char *TAG = "MODBUS_MANAGER";

// Estados de log para debug
static const char* STATE_NAMES[] = {
    "INITIALIZING", "IDLE", "RUNNING_RTU", "RUNNING_TCP", "SWITCHING", "ERROR"
};

static const char* MODE_NAMES[] = {
    "DISABLED", "RTU", "TCP", "AUTO"
};

/* ============================================================================
 * ESTRUTURA INTERNA DO GERENCIADOR
 * ============================================================================ */

typedef struct {
    // Configuração e controle
    modbus_manager_config_t config;
    modbus_mode_t desired_mode;          // Modo solicitado pelo usuário
    modbus_mode_t current_mode;          // Modo atualmente ativo
    modbus_manager_state_t state;        // Estado interno da máquina
    
    // Controle de concorrência
    SemaphoreHandle_t mutex;             // Mutex para acesso thread-safe
    
    // Handles das implementações
    TaskHandle_t rtu_task_handle;        // Handle da task RTU
    TaskHandle_t tcp_task_handle;        // Handle da task TCP (se usar)
    void* rtu_handler;                   // Handler ESP-IDF RTU
    modbus_tcp_handle_t tcp_handle;      // Handle biblioteca TCP
    
    // Controle de estado
    bool is_initialized;                 // Se foi inicializado
    bool is_running;                     // Se algum protocolo está ativo
    uint32_t uptime_start_ms;           // Timestamp da última alternância
    uint32_t last_sync_ms;              // Timestamp da última sincronização
    uint32_t last_wifi_check_ms;        // Timestamp da última verificação WiFi
    
    // Estatísticas e debug
    uint32_t rtu_message_count;         // Mensagens RTU processadas
    uint32_t tcp_connection_count;      // Conexões TCP ativas
    uint8_t error_count;                // Contador de erros consecutivos
    esp_err_t last_error;               // Último erro registrado
    char error_description[64];          // Descrição do último erro
    
    // Callback de notificação
    modbus_mode_change_callback_t mode_callback;
    
} modbus_manager_instance_t;

/* ============================================================================ 
 * INSTÂNCIA GLOBAL (SINGLETON)
 * ============================================================================ */

static modbus_manager_instance_t g_manager = {0};

/* ============================================================================
 * FUNÇÕES INTERNAS - UTILITÁRIAS
 * ============================================================================ */

/**
 * @brief Obtém timestamp atual em milissegundos
 */
static uint32_t get_timestamp_ms(void) {
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/**
 * @brief Registra erro no sistema
 */
static void log_error(esp_err_t error_code, const char* description) {
    g_manager.last_error = error_code;
    g_manager.error_count++;
    
    if (description) {
        strncpy(g_manager.error_description, description, sizeof(g_manager.error_description) - 1);
        g_manager.error_description[sizeof(g_manager.error_description) - 1] = '\0';
    }
    
    ESP_LOGE(TAG, "❌ Erro registrado: %s (%s)", description, esp_err_to_name(error_code));
}

/**
 * @brief Limpa contadores de erro (após sucesso)
 */
static void clear_error_state(void) {
    g_manager.error_count = 0;
    g_manager.last_error = ESP_OK;
    memset(g_manager.error_description, 0, sizeof(g_manager.error_description));
}

/**
 * @brief Verifica se modo requer WiFi
 */
static bool mode_requires_wifi(modbus_mode_t mode) {
    return (mode == MODBUS_MODE_TCP);
}

/**
 * @brief Verifica conectividade WiFi
 */
static bool is_wifi_connected(void) {
    wifi_status_t wifi_status = wifi_get_status();
    ESP_LOGI(TAG, "Status WiFi - Conectado: %d, IP: %s", 
             wifi_status.is_connected, 
             wifi_status.ip_address[0] != '\0' ? wifi_status.ip_address : "Não atribuído");
    return (wifi_status.is_connected && wifi_status.ip_address[0] != '\0');
}

/* ============================================================================
 * FUNÇÕES INTERNAS - IMPLEMENTAÇÕES MODBUS
 * ============================================================================ */

/**
 * @brief Para implementação RTU ativa
 */
static esp_err_t stop_rtu_implementation(void) {
    ESP_LOGI(TAG, "🛑 Parando implementação RTU...");
    
    // Para task RTU se estiver rodando
    if (g_manager.rtu_task_handle != NULL) {
        vTaskDelete(g_manager.rtu_task_handle);
        g_manager.rtu_task_handle = NULL;
        ESP_LOGI(TAG, "✅ Task RTU finalizada");
    }
    
    // Finaliza handler Modbus RTU se ativo
    if (g_manager.rtu_handler != NULL) {
        // NOTA: ESP-IDF pode não ter função específica de cleanup
        // Dependendo da implementação, pode ser necessário chamadas específicas
        g_manager.rtu_handler = NULL;
        ESP_LOGI(TAG, "✅ Handler RTU limpo");
    }
    
    return ESP_OK;
}

/**
 * @brief Para implementação TCP ativa  
 */
static esp_err_t stop_tcp_implementation(void) {
    ESP_LOGI(TAG, "🛑 Parando implementação TCP...");
    
    // Para biblioteca TCP customizada
    if (g_manager.tcp_handle != NULL) {
        esp_err_t ret = modbus_tcp_slave_stop(g_manager.tcp_handle);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "⚠️ Aviso ao parar TCP: %s", esp_err_to_name(ret));
        }
        
        modbus_tcp_slave_destroy(g_manager.tcp_handle);
        g_manager.tcp_handle = NULL;
        ESP_LOGI(TAG, "✅ Handle TCP finalizado");
    }
    
    // Para task TCP se houver uma separada
    if (g_manager.tcp_task_handle != NULL) {
        vTaskDelete(g_manager.tcp_task_handle);
        g_manager.tcp_task_handle = NULL;
        ESP_LOGI(TAG, "✅ Task TCP finalizada");
    }
    
    return ESP_OK;
}

/**
 * @brief Inicia implementação RTU
 */
static esp_err_t start_rtu_implementation(void) {
    ESP_LOGI(TAG, "🚀 Iniciando implementação RTU...");
    
    // Cria task RTU usando a implementação existente
    BaseType_t task_created = xTaskCreate(
        modbus_slave_task,           // Função da task existente
        "Modbus RTU Task",           // Nome da task
        4096,                        // Tamanho da pilha
        NULL,                        // Parâmetros (não usado)
        3,                           // Prioridade
        &g_manager.rtu_task_handle   // Handle para controle
    );
    
    if (task_created != pdTRUE) {
        log_error(ESP_ERR_NO_MEM, "Falha ao criar task RTU");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "✅ Task RTU criada com sucesso");
    
    // Aguarda um tempo para task inicializar
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    return ESP_OK;
}

/**
 * @brief Inicia implementação TCP
 */
static esp_err_t start_tcp_implementation(void) {
    ESP_LOGI(TAG, "🚀 Iniciando implementação TCP...");
    
    // Verifica se WiFi está conectado
    if (!is_wifi_connected()) {
        log_error(ESP_ERR_WIFI_NOT_CONNECT, "WiFi não conectado para TCP");
        return ESP_ERR_WIFI_NOT_CONNECT;
    }
    
    // Obtém a interface de rede WiFi e loga informações para debug
    esp_netif_t* wifi_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (wifi_netif == NULL) {
        ESP_LOGW(TAG, "⚠️ esp_netif_get_handle_from_ifkey(\"WIFI_STA_DEF\") retornou NULL");
    } else {
        ESP_LOGI(TAG, "esp_netif handle: %p", (void*)wifi_netif);
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(wifi_netif, &ip_info) == ESP_OK) {
            char ipstr[16];
            snprintf(ipstr, sizeof(ipstr), "%d.%d.%d.%d",
                     IP2STR(&ip_info.ip));
            ESP_LOGI(TAG, "esp_netif IP: %s", ipstr);
        } else {
            ESP_LOGW(TAG, "Não foi possível obter ip_info da interface");
        }
    }

    // Configuração da biblioteca TCP customizada
    modbus_tcp_config_t tcp_config = {
        .port = 502,              // Porta Modbus padrão
        .slave_id = 1,            // Endereço slave padrão
        .max_connections = 5,     // Máximo 5 conexões simultâneas
        .netif = wifi_netif,      // Interface WiFi Station (pode ser NULL)
        .auto_start = false       // NÃO auto-iniciar o servidor aqui (vamos controlar o start)
    };
    
    // Adiciona delay antes de inicializar TCP
    vTaskDelay(pdMS_TO_TICKS(2000));  // 2 segundos de delay

    // Se já existe um handle TCP, limpa primeiro
    if (g_manager.tcp_handle != NULL) {
        modbus_tcp_slave_destroy(g_manager.tcp_handle);
        g_manager.tcp_handle = NULL;
    }

    // Inicializa biblioteca TCP com retry (sem auto-start)
    int retry_count = 0;
    esp_err_t ret;
    do {
        ret = modbus_tcp_slave_init(&tcp_config, &g_manager.tcp_handle);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Tentativa %d de inicializar TCP falhou: %s", 
                     retry_count + 1, esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    } while (ret != ESP_OK && ++retry_count < 3);

    if (ret != ESP_OK) {
        log_error(ret, "Falha ao inicializar biblioteca TCP após 3 tentativas");
        return ret;
    }

    // Verifica estado retornado pela init. Se a instância já estiver RUNNING, não chamamos start.
    // Caso contrário, inicia servidor TCP com retry
    retry_count = 0;
    do {
        // Pergunta o estado atual
        modbus_tcp_state_t st = modbus_tcp_slave_get_state(g_manager.tcp_handle);
        if (st == MODBUS_TCP_STATE_RUNNING) {
            ret = ESP_OK; // já rodando
        } else {
            ret = modbus_tcp_slave_start(g_manager.tcp_handle);
        }
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Tentativa %d de iniciar TCP falhou: %s", 
                     retry_count + 1, esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    } while (ret != ESP_OK && ++retry_count < 3);

    if (ret != ESP_OK) {
        modbus_tcp_slave_destroy(g_manager.tcp_handle);
        g_manager.tcp_handle = NULL;
        log_error(ret, "Falha ao iniciar servidor TCP após 3 tentativas");
        return ret;
    }
    
    // Obtém informações de conexão para debug
    uint8_t connection_count;
    uint16_t port;
    modbus_tcp_get_connection_info(g_manager.tcp_handle, &connection_count, &port);
    
    ESP_LOGI(TAG, "✅ Servidor TCP iniciado - Porta: %d, Conexões: %d", port, connection_count);
    ESP_LOGI(TAG, "🌐 IP do servidor: %s", wifi_get_status().ip_address);
    
    return ESP_OK;
}

/* ============================================================================
 * FUNÇÕES INTERNAS - SINCRONIZAÇÃO DE REGISTRADORES
 * ============================================================================ */

/**
 * @brief Sincroniza registradores RTU → TCP
 */
static esp_err_t sync_registers_rtu_to_tcp(void) {
    if (g_manager.tcp_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Usa função de sincronização dedicada
    return modbus_sync_all_registers_rtu_to_tcp(g_manager.tcp_handle);
}

/**
 * @brief Sincroniza registradores TCP → RTU
 */
static esp_err_t sync_registers_tcp_to_rtu(void) {
    if (g_manager.tcp_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Usa função de sincronização dedicada
    return modbus_sync_all_registers_tcp_to_rtu(g_manager.tcp_handle);
}

/* ============================================================================
 * FUNÇÕES INTERNAS - TRANSIÇÕES DE ESTADO
 * ============================================================================ */

/**
 * @brief Executa transição para modo especificado
 */
static esp_err_t execute_mode_transition(modbus_mode_t new_mode) {
    ESP_LOGI(TAG, "🔄 Executando transição: %s → %s", 
             MODE_NAMES[g_manager.current_mode], MODE_NAMES[new_mode]);
    
    g_manager.state = MANAGER_STATE_SWITCHING;
    esp_err_t result = ESP_OK;
    
    // Para implementação atual
    switch (g_manager.current_mode) {
        case MODBUS_MODE_RTU:
            result = stop_rtu_implementation();
            break;
        case MODBUS_MODE_TCP:
            result = stop_tcp_implementation();
            break;
        default:
            break;
    }
    
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "❌ Falha ao parar modo atual: %s", esp_err_to_name(result));
        g_manager.state = MANAGER_STATE_ERROR;
        return result;
    }
    
    // Aguarda estabilização
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Inicia nova implementação
    switch (new_mode) {
        case MODBUS_MODE_DISABLED:
            g_manager.state = MANAGER_STATE_IDLE;
            g_manager.is_running = false;
            break;
            
        case MODBUS_MODE_RTU:
            result = start_rtu_implementation();
            if (result == ESP_OK) {
                g_manager.state = MANAGER_STATE_RUNNING_RTU;
                g_manager.is_running = true;
            }
            break;
            
        case MODBUS_MODE_TCP:
            result = start_tcp_implementation();
            if (result == ESP_OK) {
                g_manager.state = MANAGER_STATE_RUNNING_TCP;
                g_manager.is_running = true;
            }
            break;
            
        case MODBUS_MODE_AUTO:
            // Decide automaticamente com base na conectividade
            if (is_wifi_connected()) {
                result = execute_mode_transition(MODBUS_MODE_TCP);
            } else {
                result = execute_mode_transition(MODBUS_MODE_RTU);
            }
            return result; // Evita processamento duplo
            
        default:
            result = ESP_ERR_INVALID_ARG;
            break;
    }
    
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "❌ Falha ao iniciar novo modo: %s", esp_err_to_name(result));
        g_manager.state = MANAGER_STATE_ERROR;
        log_error(result, "Transição de modo falhada");
        return result;
    }
    
    // Atualiza estado com sucesso
    modbus_mode_t old_mode = g_manager.current_mode;
    g_manager.current_mode = new_mode;
    g_manager.uptime_start_ms = get_timestamp_ms();
    clear_error_state();
    
    // Chama callback se registrado
    if (g_manager.mode_callback != NULL) {
        g_manager.mode_callback(old_mode, new_mode);
    }
    
    ESP_LOGI(TAG, "✅ Transição concluída com sucesso: %s ativo", MODE_NAMES[new_mode]);
    return ESP_OK;
}

/* ============================================================================
 * FUNÇÕES INTERNAS - LÓGICA PRINCIPAL
 * ============================================================================ */

/**
 * @brief Processa lógica da máquina de estados
 */
static void process_state_machine(void) {
    uint32_t current_time_ms = get_timestamp_ms();
    
    // Verifica se houve solicitação de mudança de modo
    if (g_manager.desired_mode != g_manager.current_mode && 
        g_manager.state != MANAGER_STATE_SWITCHING) {
        
        execute_mode_transition(g_manager.desired_mode);
        return;
    }
    
    // Lógica específica por estado
    switch (g_manager.state) {
        case MANAGER_STATE_INITIALIZING:
            // Transita para IDLE após inicialização
            g_manager.state = MANAGER_STATE_IDLE;
            ESP_LOGI(TAG, "📍 Estado: IDLE (pronto para operação)");
            break;
            
        case MANAGER_STATE_IDLE:
            // Aguarda solicitação de modo
            break;
            
        case MANAGER_STATE_RUNNING_RTU:
        case MANAGER_STATE_RUNNING_TCP:
            // Sincroniza registradores periodicamente
            if (g_manager.config.register_sync_enabled && 
                current_time_ms - g_manager.last_sync_ms >= g_manager.config.sync_interval_ms) {
                
                if (g_manager.state == MANAGER_STATE_RUNNING_RTU) {
                    sync_registers_rtu_to_tcp();
                } else {
                    sync_registers_tcp_to_rtu();
                }
                
                g_manager.last_sync_ms = current_time_ms;
            }
            
            // Verifica conectividade WiFi para modo AUTO
            if (g_manager.desired_mode == MODBUS_MODE_AUTO &&
                current_time_ms - g_manager.last_wifi_check_ms >= g_manager.config.wifi_check_interval_ms) {
                
                bool wifi_connected = is_wifi_connected();
                
                // Fallback automático se WiFi caiu durante TCP
                if (g_manager.state == MANAGER_STATE_RUNNING_TCP && !wifi_connected && 
                    g_manager.config.auto_fallback_enabled) {
                    ESP_LOGW(TAG, "⚠️ WiFi desconectado, fazendo fallback para RTU");
                    execute_mode_transition(MODBUS_MODE_RTU);
                }
                // Upgrade automático se WiFi voltou durante RTU  
                else if (g_manager.state == MANAGER_STATE_RUNNING_RTU && wifi_connected) {
                    ESP_LOGI(TAG, "📶 WiFi conectado, alternando para TCP");
                    execute_mode_transition(MODBUS_MODE_TCP);
                }
                
                g_manager.last_wifi_check_ms = current_time_ms;
            }
            break;
            
        case MANAGER_STATE_SWITCHING:
            // Aguarda conclusão da transição (processada em execute_mode_transition)
            break;
            
        case MANAGER_STATE_ERROR:
            // Tenta recuperação após algumas tentativas
            if (g_manager.error_count >= g_manager.config.max_retry_attempts) {
                ESP_LOGE(TAG, "❌ Muitos erros consecutivos, permanecendo em estado de erro");
            } else {
                ESP_LOGW(TAG, "🔄 Tentando recuperação automática...");
                vTaskDelay(pdMS_TO_TICKS(5000)); // Aguarda 5s antes de tentar
                execute_mode_transition(MODBUS_MODE_RTU); // Fallback para RTU
            }
            break;
    }
}

/* ============================================================================
 * API PÚBLICA - IMPLEMENTAÇÃO
 * ============================================================================ */

esp_err_t modbus_manager_init(const modbus_manager_config_t *config) {
    if (g_manager.is_initialized) {
        ESP_LOGW(TAG, "⚠️ Manager já foi inicializado");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "🔧 Inicializando Modbus Manager...");
    
    // Limpa estrutura
    memset(&g_manager, 0, sizeof(modbus_manager_instance_t));
    
    // Aplica configuração padrão ou fornecida
    if (config != NULL) {
        g_manager.config = *config;
    } else {
        g_manager.config.sync_interval_ms = MODBUS_MANAGER_DEFAULT_SYNC_INTERVAL_MS;
        g_manager.config.wifi_check_interval_ms = MODBUS_MANAGER_DEFAULT_WIFI_CHECK_INTERVAL_MS;
        g_manager.config.auto_fallback_enabled = true;
        g_manager.config.register_sync_enabled = true;
        g_manager.config.max_retry_attempts = MODBUS_MANAGER_DEFAULT_MAX_RETRY_ATTEMPTS;
    }
    
    // Cria mutex para acesso thread-safe
    g_manager.mutex = xSemaphoreCreateMutex();
    if (g_manager.mutex == NULL) {
        ESP_LOGE(TAG, "❌ Falha ao criar mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Inicializa timestamps
    uint32_t now = get_timestamp_ms();
    g_manager.uptime_start_ms = now;
    g_manager.last_sync_ms = now;
    g_manager.last_wifi_check_ms = now;
    
    // Lê modo inicial da configuração
    g_manager.desired_mode = modbus_manager_read_config_mode();
    g_manager.current_mode = MODBUS_MODE_DISABLED;
    g_manager.state = MANAGER_STATE_INITIALIZING;
    
    g_manager.is_initialized = true;
    
    ESP_LOGI(TAG, "✅ Modbus Manager inicializado (modo inicial: %s)", 
             MODE_NAMES[g_manager.desired_mode]);
    
    return ESP_OK;
}

void modbus_manager_task(void *pvParameters) {
    ESP_LOGI(TAG, "🎯 Modbus Manager Task iniciada");
    
    // Inicializa se não foi feito antes
    if (!g_manager.is_initialized) {
        esp_err_t ret = modbus_manager_init(NULL);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "❌ Falha crítica na inicialização: %s", esp_err_to_name(ret));
            vTaskDelete(NULL);
            return;
        }
    }
    
    // Loop principal da task
    while (true) {
        // Processa máquina de estados
        if (xSemaphoreTake(g_manager.mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            process_state_machine();
            xSemaphoreGive(g_manager.mutex);
        }
        
        // Aguarda antes do próximo ciclo
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t modbus_manager_switch_mode(modbus_mode_t new_mode) {
    if (!g_manager.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (new_mode > MODBUS_MODE_AUTO) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "📋 Solicitação de mudança de modo: %s → %s", 
             MODE_NAMES[g_manager.current_mode], MODE_NAMES[new_mode]);
    
    if (xSemaphoreTake(g_manager.mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        g_manager.desired_mode = new_mode;
        xSemaphoreGive(g_manager.mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

modbus_mode_t modbus_manager_get_mode(void) {
    modbus_mode_t mode = MODBUS_MODE_DISABLED;
    
    if (xSemaphoreTake(g_manager.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        mode = g_manager.current_mode;
        xSemaphoreGive(g_manager.mutex);
    }
    
    return mode;
}

bool modbus_manager_is_running(void) {
    bool running = false;
    
    if (xSemaphoreTake(g_manager.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        running = g_manager.is_running;
        xSemaphoreGive(g_manager.mutex);
    }
    
    return running;
}

esp_err_t modbus_manager_get_status(modbus_status_t *status) {
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_manager.mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        status->mode = g_manager.current_mode;
        status->state = g_manager.state;
        status->is_running = g_manager.is_running;
        status->wifi_available = is_wifi_connected();
        status->uptime_seconds = (get_timestamp_ms() - g_manager.uptime_start_ms) / 1000;
        status->rtu_message_count = g_manager.rtu_message_count;
        status->tcp_connection_count = g_manager.tcp_connection_count;
        status->last_error = g_manager.last_error;
        strncpy(status->error_description, g_manager.error_description, 
                sizeof(status->error_description) - 1);
        
        xSemaphoreGive(g_manager.mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

modbus_mode_t modbus_manager_read_config_mode(void) {
    modbus_mode_t mode = MODBUS_MODE_RTU; // Padrão seguro
    
    FILE *f = fopen("/spiffs/config.json", "r");
    if (f == NULL) {
        ESP_LOGW(TAG, "⚠️ Arquivo config.json não encontrado, usando modo RTU padrão");
        return mode;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    
    char *data = malloc(size + 1);
    if (data == NULL) {
        ESP_LOGE(TAG, "❌ Falha ao alocar memória para config.json");
        fclose(f);
        return mode;
    }
    
    fread(data, 1, size, f);
    data[size] = '\0';
    fclose(f);
    
    cJSON *root = cJSON_Parse(data);
    if (root != NULL) {
        cJSON *mode_item = cJSON_GetObjectItem(root, "modbus_mode");
        if (mode_item != NULL && cJSON_IsString(mode_item)) {
            const char *mode_str = mode_item->valuestring;
            
            if (strcmp(mode_str, "rtu") == 0) {
                mode = MODBUS_MODE_RTU;
            } else if (strcmp(mode_str, "tcp") == 0) {
                mode = MODBUS_MODE_TCP;
            } else if (strcmp(mode_str, "auto") == 0) {
                mode = MODBUS_MODE_AUTO;
            } else if (strcmp(mode_str, "disabled") == 0) {
                mode = MODBUS_MODE_DISABLED;
            }
        }
        cJSON_Delete(root);
    }
    
    free(data);
    
    ESP_LOGI(TAG, "📖 Modo lido da configuração: %s", MODE_NAMES[mode]);
    return mode;
}

esp_err_t modbus_manager_save_config_mode(modbus_mode_t mode) {
    ESP_LOGI(TAG, "💾 Salvando modo na configuração: %s", MODE_NAMES[mode]);
    
    // TODO: Implementar salvamento no config.json
    // Por enquanto, apenas atualiza modo desejado
    return modbus_manager_switch_mode(mode);
}

esp_err_t modbus_manager_sync_registers(void) {
    if (!g_manager.is_initialized || !g_manager.is_running) {
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t result = ESP_OK;
    
    if (xSemaphoreTake(g_manager.mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        switch (g_manager.current_mode) {
            case MODBUS_MODE_RTU:
                result = sync_registers_rtu_to_tcp();
                break;
            case MODBUS_MODE_TCP:
                result = sync_registers_tcp_to_rtu();
                break;
            default:
                result = ESP_ERR_INVALID_STATE;
                break;
        }
        
        xSemaphoreGive(g_manager.mutex);
    } else {
        result = ESP_ERR_TIMEOUT;
    }
    
    return result;
}

esp_err_t modbus_manager_set_mode_callback(modbus_mode_change_callback_t callback) {
    if (xSemaphoreTake(g_manager.mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        g_manager.mode_callback = callback;
        xSemaphoreGive(g_manager.mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

esp_err_t modbus_manager_emergency_stop(void) {
    ESP_LOGW(TAG, "🚨 PARADA DE EMERGÊNCIA ACIONADA!");
    
    if (xSemaphoreTake(g_manager.mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
        stop_rtu_implementation();
        stop_tcp_implementation();
        
        g_manager.current_mode = MODBUS_MODE_DISABLED;
        g_manager.desired_mode = MODBUS_MODE_DISABLED;
        g_manager.state = MANAGER_STATE_IDLE;
        g_manager.is_running = false;
        
        xSemaphoreGive(g_manager.mutex);
        
        ESP_LOGW(TAG, "🛑 Parada de emergência concluída");
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}