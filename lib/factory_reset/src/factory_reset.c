/*
 * ========================================================================
 * FACTORY RESET LIBRARY - IMPLEMENTAÇÃO
 * ========================================================================
 */

#include "factory_reset.h"

// Tenta incluir event_bus se disponível (integração opcional)
#ifdef __has_include
    #if __has_include("event_bus.h")
        #include "event_bus.h"
        #define HAS_EVENT_BUS 1
    #endif
#endif

// ========================================================================
// VARIÁVEIS PRIVADAS
// ========================================================================

static const char *TAG = "FACTORY_RESET";

// Configuração atual da biblioteca
static factory_reset_config_t current_config;
static bool is_initialized = false;

// Controle de estado
static factory_reset_state_t current_state = FACTORY_RESET_STATE_IDLE;
static SemaphoreHandle_t state_mutex = NULL;

// Task de monitoramento do botão
static TaskHandle_t button_monitor_task_handle = NULL;
static volatile bool button_monitoring_active = false;

// Callback de eventos
static factory_reset_callback_t event_callback = NULL;

// ========================================================================
// FUNÇÕES PRIVADAS - UTILITÁRIOS
// ========================================================================

/**
 * @brief Atualiza estado de forma thread-safe
 */
static void set_state(factory_reset_state_t new_state) {
    if (xSemaphoreTake(state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        ESP_LOGI(TAG, "Estado: %d -> %d", current_state, new_state);
        current_state = new_state;
        
        // Chama callback se registrado
        if (event_callback != NULL) {
            event_callback(FACTORY_RESET_TYPE_BUTTON, new_state);
        }
        
        xSemaphoreGive(state_mutex);
    }
}

/**
 * @brief Controla LED de feedback
 */
static void control_led(bool state) {
    if (current_config.enable_led_feedback) {
        gpio_set_level(current_config.led_gpio, state ? 1 : 0);
    }
}

/**
 * @brief Pisca LED durante reset
 */
static void blink_led_task(void *param) {
    uint32_t blink_count = *((uint32_t*)param);
    
    for (uint32_t i = 0; i < blink_count; i++) {
        control_led(true);
        vTaskDelay(pdMS_TO_TICKS(FACTORY_RESET_LED_BLINK_PERIOD_MS));
        control_led(false);
        vTaskDelay(pdMS_TO_TICKS(FACTORY_RESET_LED_BLINK_PERIOD_MS));
    }
    
    vTaskDelete(NULL);
}

/**
 * @brief Remove arquivos de configuração do SPIFFS
 */
static esp_err_t remove_spiffs_config_files(void) {
    esp_err_t result = ESP_OK;
    
    // Lista de arquivos a remover
    const char* config_files[] = {
        "/data/config/rtu_config.json",
        "/data/config/ap_config.json",
        "/data/config/sta_config.json",
        "/data/config/mqtt_config.json",
        "/data/config/network_config.json"
    };
    
    size_t file_count = sizeof(config_files) / sizeof(config_files[0]);
    
    for (size_t i = 0; i < file_count; i++) {
        if (remove(config_files[i]) != 0) {
            ESP_LOGW(TAG, "Arquivo %s não encontrado ou já removido", config_files[i]);
        } else {
            ESP_LOGI(TAG, "Arquivo %s removido com sucesso", config_files[i]);
        }
    }
    
    return result;
}

// ========================================================================
// FUNÇÕES PRIVADAS - EXECUÇÃO DO RESET
// ========================================================================

/**
 * @brief Task que executa o factory reset de forma assíncrona
 */
static void factory_reset_async_task(void *param) {
    factory_reset_type_t type = *((factory_reset_type_t*)param);
    
    ESP_LOGI(TAG, "Iniciando Factory Reset (tipo: %d)", type);
    set_state(FACTORY_RESET_STATE_EXECUTING);
    
    // Notifica início se system event bus disponível
    #ifdef HAS_EVENT_BUS
    factory_reset_notify_start();
    #endif
    
    // Pequeno delay para garantir que logs sejam exibidos
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // 1. Pisca LED para indicar início do reset
    if (current_config.enable_led_feedback) {
        uint32_t blink_count = 5;
        xTaskCreate(blink_led_task, "LED_Blink", 1024, &blink_count, 1, NULL);
        vTaskDelay(pdMS_TO_TICKS(2000)); // Aguarda piscar
    }
    
    // 2. Apaga NVS
    ESP_LOGI(TAG, "Apagando NVS...");
    esp_err_t ret = nvs_flash_erase();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao apagar NVS: %s", esp_err_to_name(ret));
        set_state(FACTORY_RESET_STATE_ERROR);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "NVS apagado com sucesso");
    
    // 3. Remove arquivos de configuração SPIFFS
    ESP_LOGI(TAG, "Removendo arquivos de configuração...");
    remove_spiffs_config_files();
    
    // 4. Sinaliza conclusão
    ESP_LOGI(TAG, "Factory Reset concluído - reiniciando sistema em 2 segundos");
    set_state(FACTORY_RESET_STATE_COMPLETED);
    
    #ifdef HAS_EVENT_BUS
    factory_reset_notify_complete();
    #endif
    
    // LED sólido para indicar conclusão
    control_led(true);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 5. Reinicia ESP32
    ESP_LOGI(TAG, "Reiniciando ESP32...");
    esp_restart();
    
    // Esta linha nunca será alcançada
    vTaskDelete(NULL);
}

/**
 * @brief Task de monitoramento do botão de reset
 */
static void button_monitor_task(void *param) {
    ESP_LOGI(TAG, "Task de monitoramento do botão iniciada");
    
    uint32_t press_start_time = 0;
    bool button_was_pressed = false;
    bool reset_triggered = false;
    
    button_monitoring_active = true;
    
    while (button_monitoring_active && !reset_triggered) {
        bool button_pressed = !gpio_get_level(current_config.button_gpio); // Assumindo pullup
        
        if (button_pressed && !button_was_pressed) {
            // Início da pressão
            press_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            button_was_pressed = true;
            set_state(FACTORY_RESET_STATE_BUTTON_PRESSED);
            ESP_LOGI(TAG, "Botão pressionado - aguardando %d ms", current_config.press_time_ms);
            
            // LED começa a piscar para feedback
            control_led(true);
        }
        else if (button_pressed && button_was_pressed) {
            // Botão continua pressionado - verifica tempo
            uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            uint32_t press_duration = current_time - press_start_time;
            
            // LED pisca para indicar progresso
            if ((press_duration / FACTORY_RESET_LED_BLINK_PERIOD_MS) % 2 == 0) {
                control_led(true);
            } else {
                control_led(false);
            }
            
            if (press_duration >= current_config.press_time_ms) {
                // Tempo suficiente - executa reset
                ESP_LOGW(TAG, "Botão pressionado por %lu ms - executando Factory Reset!", press_duration);
                reset_triggered = true;
                
                factory_reset_type_t type = FACTORY_RESET_TYPE_BUTTON;
                xTaskCreate(factory_reset_async_task, "FactoryReset", 4096, &type, 5, NULL);
            }
        }
        else if (!button_pressed && button_was_pressed) {
            // Botão solto antes do tempo
            uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            uint32_t press_duration = current_time - press_start_time;
            ESP_LOGI(TAG, "Botão solto após %lu ms (necessário %d ms) - cancelado", 
                     press_duration, current_config.press_time_ms);
            
            button_was_pressed = false;
            set_state(FACTORY_RESET_STATE_IDLE);
            control_led(false);
        }
        
        vTaskDelay(pdMS_TO_TICKS(current_config.debounce_time_ms));
    }
    
    ESP_LOGI(TAG, "Task de monitoramento do botão finalizada");
    button_monitor_task_handle = NULL;
    vTaskDelete(NULL);
}

// ========================================================================
// API PÚBLICA - INICIALIZAÇÃO
// ========================================================================

esp_err_t factory_reset_init(void) {
    factory_reset_config_t default_config = FACTORY_RESET_DEFAULT_CONFIG();
    return factory_reset_init_with_config(&default_config);
}

esp_err_t factory_reset_init_with_config(const factory_reset_config_t *config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (is_initialized) {
        ESP_LOGW(TAG, "Biblioteca já inicializada");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Inicializando biblioteca Factory Reset");
    
    // Copia configuração
    memcpy(&current_config, config, sizeof(factory_reset_config_t));
    
    // Cria mutex de proteção
    state_mutex = xSemaphoreCreateMutex();
    if (state_mutex == NULL) {
        ESP_LOGE(TAG, "Falha ao criar mutex de estado");
        return ESP_ERR_NO_MEM;
    }
    
    // Configura GPIO do botão
    gpio_config_t button_config = {
        .pin_bit_mask = (1ULL << current_config.button_gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    
    esp_err_t ret = gpio_config(&button_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao configurar GPIO do botão: %s", esp_err_to_name(ret));
        vSemaphoreDelete(state_mutex);
        return ret;
    }
    
    // Configura GPIO do LED
    if (current_config.enable_led_feedback) {
        gpio_config_t led_config = {
            .pin_bit_mask = (1ULL << current_config.led_gpio),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        
        ret = gpio_config(&led_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Falha ao configurar GPIO do LED: %s", esp_err_to_name(ret));
            vSemaphoreDelete(state_mutex);
            return ret;
        }
        
        // LED começa apagado
        gpio_set_level(current_config.led_gpio, 0);
    }
    
    current_state = FACTORY_RESET_STATE_IDLE;
    is_initialized = true;
    
    ESP_LOGI(TAG, "Biblioteca Factory Reset inicializada");
    ESP_LOGI(TAG, "  Botão GPIO: %d", current_config.button_gpio);
    ESP_LOGI(TAG, "  LED GPIO: %d", current_config.led_gpio);
    ESP_LOGI(TAG, "  Tempo de pressão: %lu ms", current_config.press_time_ms);
    
    return ESP_OK;
}

esp_err_t factory_reset_deinit(void) {
    if (!is_initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Desinicializando biblioteca Factory Reset");
    
    // Para monitoramento do botão se ativo
    factory_reset_stop_button_monitoring();
    
    // Libera mutex
    if (state_mutex != NULL) {
        vSemaphoreDelete(state_mutex);
        state_mutex = NULL;
    }
    
    // Reset GPIOs
    gpio_reset_pin(current_config.button_gpio);
    if (current_config.enable_led_feedback) {
        gpio_reset_pin(current_config.led_gpio);
    }
    
    is_initialized = false;
    event_callback = NULL;
    current_state = FACTORY_RESET_STATE_IDLE;
    
    ESP_LOGI(TAG, "Biblioteca Factory Reset desinicializada");
    return ESP_OK;
}

// ========================================================================
// API PÚBLICA - CONTROLE DO BOTÃO
// ========================================================================

esp_err_t factory_reset_start_button_monitoring(void) {
    if (!is_initialized) {
        ESP_LOGE(TAG, "Biblioteca não inicializada");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!current_config.enable_button_monitoring) {
        ESP_LOGW(TAG, "Monitoramento do botão desabilitado na configuração");
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    if (button_monitor_task_handle != NULL) {
        ESP_LOGW(TAG, "Monitoramento do botão já ativo");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Iniciando monitoramento do botão de reset");
    
    BaseType_t ret = xTaskCreate(
        button_monitor_task,
        "Factory Reset Button",
        FACTORY_RESET_TASK_STACK_SIZE,
        NULL,
        2, // Prioridade baixa
        &button_monitor_task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Falha ao criar task de monitoramento do botão");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "Monitoramento do botão iniciado");
    return ESP_OK;
}

esp_err_t factory_reset_stop_button_monitoring(void) {
    if (button_monitor_task_handle == NULL) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Parando monitoramento do botão");
    
    button_monitoring_active = false;
    
    // Aguarda task finalizar (timeout de 5 segundos)
    uint32_t timeout_ms = 5000;
    uint32_t elapsed_ms = 0;
    
    while (button_monitor_task_handle != NULL && elapsed_ms < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(100));
        elapsed_ms += 100;
    }
    
    if (button_monitor_task_handle != NULL) {
        ESP_LOGW(TAG, "Task não finalizou no timeout - forçando deleção");
        vTaskDelete(button_monitor_task_handle);
        button_monitor_task_handle = NULL;
    }
    
    control_led(false);
    set_state(FACTORY_RESET_STATE_IDLE);
    
    ESP_LOGI(TAG, "Monitoramento do botão parado");
    return ESP_OK;
}

bool factory_reset_is_button_pressed(void) {
    if (!is_initialized) {
        return false;
    }
    
    return !gpio_get_level(current_config.button_gpio); // Assumindo pullup
}

// ========================================================================
// API PÚBLICA - EXECUÇÃO DE RESET
// ========================================================================

esp_err_t factory_reset_execute(factory_reset_type_t type) {
    if (!is_initialized) {
        ESP_LOGE(TAG, "Biblioteca não inicializada");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGW(TAG, "ATENÇÃO: Executando Factory Reset SÍNCRONO - sistema será reiniciado!");
    
    // Chama versão assíncrona e aguarda
    factory_reset_async_task(&type);
    
    // Esta linha nunca será alcançada devido ao esp_restart()
    return ESP_OK;
}

esp_err_t factory_reset_execute_async(factory_reset_type_t type) {
    if (!is_initialized) {
        ESP_LOGE(TAG, "Biblioteca não inicializada");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (current_state != FACTORY_RESET_STATE_IDLE) {
        ESP_LOGE(TAG, "Factory reset já em progresso (estado: %d)", current_state);
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Iniciando Factory Reset assíncrono (tipo: %d)", type);
    
    BaseType_t ret = xTaskCreate(
        factory_reset_async_task,
        "Factory Reset Exec",
        4096,
        &type,
        5, // Prioridade alta
        NULL
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Falha ao criar task de execução do reset");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

// ========================================================================
// API PÚBLICA - WEBSERVER INTEGRATION
// ========================================================================

#ifdef CONFIG_ESP_HTTP_SERVER_ENABLE

esp_err_t factory_reset_web_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Factory reset solicitado via web");
    
    // Responde imediatamente ao cliente
    httpd_resp_send(req, "Factory Reset iniciado - sistema será reiniciado", HTTPD_RESP_USE_STRLEN);
    
    // Pequeno delay para garantir que resposta seja enviada
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Executa reset de forma assíncrona
    esp_err_t ret = factory_reset_execute_async(FACTORY_RESET_TYPE_WEB);
    
    return ret;
}

#endif // CONFIG_ESP_HTTP_SERVER_ENABLE

// ========================================================================
// API PÚBLICA - UTILITÁRIOS
// ========================================================================

factory_reset_state_t factory_reset_get_state(void) {
    factory_reset_state_t state;
    
    if (xSemaphoreTake(state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        state = current_state;
        xSemaphoreGive(state_mutex);
    } else {
        state = FACTORY_RESET_STATE_ERROR;
    }
    
    return state;
}

bool factory_reset_is_in_progress(void) {
    factory_reset_state_t state = factory_reset_get_state();
    return (state == FACTORY_RESET_STATE_EXECUTING || 
            state == FACTORY_RESET_STATE_BUTTON_PRESSED);
}

esp_err_t factory_reset_register_callback(factory_reset_callback_t callback) {
    if (callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    event_callback = callback;
    ESP_LOGI(TAG, "Callback de eventos registrado");
    return ESP_OK;
}

esp_err_t factory_reset_unregister_callback(void) {
    event_callback = NULL;
    ESP_LOGI(TAG, "Callback de eventos removido");
    return ESP_OK;
}

// ========================================================================
// API PÚBLICA - INTEGRAÇÃO COM SISTEMA DE EVENTOS
// ========================================================================

esp_err_t factory_reset_notify_start(void) {
    #ifdef HAS_EVENT_BUS
    return eventbus_factory_reset_start();
    #else
    ESP_LOGW(TAG, "Sistema de eventos não disponível");
    return ESP_ERR_NOT_SUPPORTED;
    #endif
}

esp_err_t factory_reset_notify_complete(void) {
    #ifdef HAS_EVENT_BUS
    return eventbus_factory_reset_complete();
    #else
    ESP_LOGW(TAG, "Sistema de eventos não disponível");
    return ESP_ERR_NOT_SUPPORTED;
    #endif
}