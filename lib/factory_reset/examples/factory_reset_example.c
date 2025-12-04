/*
 * ========================================================================
 * EXEMPLO DE USO - FACTORY RESET LIBRARY
 * ========================================================================
 * 
 * Este exemplo demonstra como usar a biblioteca factory_reset em
 * diferentes cenários de um sistema ESP32.
 * 
 * CENÁRIOS DEMONSTRADOS:
 * 1. Inicialização básica com configuração padrão
 * 2. Configuração personalizada 
 * 3. Integração com webserver
 * 4. Uso de callbacks para monitoramento
 * 5. Controle programático
 * 
 * ========================================================================
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "factory_reset.h"

static const char *TAG = "FACTORY_RESET_EXAMPLE";

// ========================================================================
// EXEMPLO 1: USO BÁSICO COM CONFIGURAÇÃO PADRÃO
// ========================================================================

void example_basic_usage(void) {
    ESP_LOGI(TAG, "=== EXEMPLO 1: Uso Básico ===");
    
    // Inicializa com configuração padrão
    // Botão: GPIO 4, LED: GPIO 2, Tempo: 3 segundos
    esp_err_t ret = factory_reset_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar factory_reset: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "Factory Reset inicializado com configuração padrão");
    
    // Inicia monitoramento do botão em background
    ret = factory_reset_start_button_monitoring();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar monitoramento do botão: %s", esp_err_to_name(ret));
        return;
    }
    
        ESP_LOGI(TAG, "Pressione GPIO 5 por 3 segundos para reset");
}

// ========================================================================
// EXEMPLO 2: CONFIGURAÇÃO PERSONALIZADA
// ========================================================================

void example_custom_config(void) {
    ESP_LOGI(TAG, "=== EXEMPLO 2: Configuração Personalizada ===");
    
    // Configuração customizada
    factory_reset_config_t custom_config = {
        .button_gpio = GPIO_NUM_0,          // Botão BOOT do ESP32
        .led_gpio = GPIO_NUM_2,             // LED interno
        .press_time_ms = 5000,              // 5 segundos em vez de 3
        .debounce_time_ms = 100,            // Debounce de 100ms
        .enable_button_monitoring = true,    // Habilita monitoramento
        .enable_led_feedback = true         // Habilita feedback LED
    };
    
    esp_err_t ret = factory_reset_init_with_config(&custom_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar com config personalizada: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "Factory Reset configurado:");
    ESP_LOGI(TAG, "  - Botão: GPIO 0 (BOOT)");
    ESP_LOGI(TAG, "  - LED: GPIO 2"); 
    ESP_LOGI(TAG, "  - Tempo: 5 segundos");
    
    factory_reset_start_button_monitoring();
}

// ========================================================================
// EXEMPLO 3: CALLBACK DE EVENTOS
// ========================================================================

void factory_reset_event_callback(factory_reset_type_t type, factory_reset_state_t state) {
    ESP_LOGI(TAG, "Evento Factory Reset - Tipo: %d, Estado: %d", type, state);
    
    switch (state) {
        case FACTORY_RESET_STATE_IDLE:
            ESP_LOGI(TAG, "   Sistema inativo");
            break;
        case FACTORY_RESET_STATE_BUTTON_PRESSED:
            ESP_LOGI(TAG, "   Botão pressionado - mantenha pressionado!");
            break;
        case FACTORY_RESET_STATE_EXECUTING:
            ESP_LOGI(TAG, "   EXECUTANDO RESET - NÃO DESLIGUE O ESP32!");
            break;
        case FACTORY_RESET_STATE_COMPLETED:
            ESP_LOGI(TAG, "   Reset concluído - sistema será reiniciado");
            break;
        case FACTORY_RESET_STATE_ERROR:
            ESP_LOGI(TAG, "   Erro durante reset");
            break;
    }
}

void example_with_callback(void) {
    ESP_LOGI(TAG, "=== EXEMPLO 3: Com Callback de Eventos ===");
    
    factory_reset_init();
    
    // Registra callback para receber notificações
    esp_err_t ret = factory_reset_register_callback(factory_reset_event_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao registrar callback: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "Callback registrado - você receberá notificações dos eventos");
    
    factory_reset_start_button_monitoring();
}

// ========================================================================
// EXEMPLO 4: INTEGRAÇÃO COM WEBSERVER
// ========================================================================

// Handler simples de página inicial
esp_err_t home_handler(httpd_req_t *req) {
    const char* html = 
        "<!DOCTYPE html>"
        "<html><head><title>Factory Reset Example</title></head>"
        "<body>"
        "<h1>Factory Reset Example</h1>"
        "<p>Sistema com Factory Reset integrado</p>"
        "<form method='POST' action='/factory_reset'>"
        "<button type='submit' onclick='return confirm(\"Tem certeza? Isso apagará todas as configurações!\")'"
        " style='background-color: red; color: white; padding: 10px 20px; font-size: 16px;'>"
        "FACTORY RESET"
        "</button>"
        "</form>"
        "</body></html>";
    
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void example_webserver_integration(void) {
    ESP_LOGI(TAG, "=== EXEMPLO 4: Integração com WebServer ===");
    
    // Inicializa factory reset
    factory_reset_init();
    factory_reset_start_button_monitoring();
    
    // Configura servidor HTTP
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    
    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar servidor HTTP: %s", esp_err_to_name(ret));
        return;
    }
    
    // Registra página inicial
    httpd_uri_t home_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = home_handler
    };
    httpd_register_uri_handler(server, &home_uri);
    
    // Registra endpoint de factory reset usando a biblioteca
    httpd_uri_t reset_uri = {
        .uri = "/factory_reset",
        .method = HTTP_POST,
        .handler = factory_reset_web_handler  // ← Handler da biblioteca!
    };
    httpd_register_uri_handler(server, &reset_uri);
    
    ESP_LOGI(TAG, "Servidor HTTP iniciado");
    ESP_LOGI(TAG, "Acesse: http://192.168.4.1/ (se AP ativo)");
    ESP_LOGI(TAG, "Endpoint de reset: POST /factory_reset");
}

// ========================================================================
// EXEMPLO 5: CONTROLE PROGRAMÁTICO
// ========================================================================

void system_monitoring_task(void *param) {
    ESP_LOGI(TAG, "Task de monitoramento do sistema iniciada");
    
    while (1) {
        // Simula condições que podem requerer factory reset
        
        // Exemplo: Verifica se botão está sendo pressionado
        if (factory_reset_is_button_pressed()) {
            ESP_LOGI(TAG, "Botão detectado via API (leitura única)");
        }
        
        // Exemplo: Verifica estado atual
        factory_reset_state_t state = factory_reset_get_state();
        static factory_reset_state_t last_state = FACTORY_RESET_STATE_IDLE;
        
        if (state != last_state) {
            ESP_LOGI(TAG, "Estado mudou: %d -> %d", last_state, state);
            last_state = state;
        }
        
        // Exemplo: Reset programático baseado em condição
        static int error_count = 0;
        error_count++;
        
        // Simula: se muitos erros, executa reset automático
        if (error_count > 1000) { // Na prática seria algo como falhas de rede
            ESP_LOGW(TAG, "Muitos erros detectados - executando factory reset programático");
            factory_reset_execute_async(FACTORY_RESET_TYPE_API);
            break; // Sai do loop pois sistema será reiniciado
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // Verifica a cada segundo
    }
    
    vTaskDelete(NULL);
}

void example_programmatic_control(void) {
    ESP_LOGI(TAG, "=== EXEMPLO 5: Controle Programático ===");
    
    factory_reset_init();
    factory_reset_register_callback(factory_reset_event_callback);
    factory_reset_start_button_monitoring();
    
    // Cria task para monitoramento e controle automático
    xTaskCreate(system_monitoring_task, "SysMonitor", 2048, NULL, 2, NULL);
    
    ESP_LOGI(TAG, "Sistema com controle automático iniciado");
    ESP_LOGI(TAG, "Factory reset será executado automaticamente se muitos erros ocorrerem");
}

// ========================================================================
// FUNÇÃO PRINCIPAL DE EXEMPLO
// ========================================================================

void app_main(void) {
    ESP_LOGI(TAG, "========== FACTORY RESET LIBRARY - EXEMPLOS ==========");
    
    // Descomente o exemplo que deseja testar:
    
    // Exemplo básico
    // example_basic_usage();
    
    // Configuração personalizada  
    // example_custom_config();
    
    // Com callback de eventos
    // example_with_callback();
    
    // Integração com webserver
    // example_webserver_integration();
    
    // Controle programático
    example_programmatic_control();
    
    ESP_LOGI(TAG, "Exemplo iniciado. Sistema operacional.");
    
    // Loop principal - em um sistema real, outras tasks rodariam aqui
    while (1) {
        ESP_LOGI(TAG, "Sistema rodando... (pressione botão para testar factory reset)");
        vTaskDelay(pdMS_TO_TICKS(10000)); // Log a cada 10 segundos
    }
}

// ========================================================================
// EXEMPLO DE TESTE RÁPIDO (SEM WEBSERVER)
// ========================================================================

void quick_test_example(void) {
    ESP_LOGI(TAG, "=== TESTE RÁPIDO ===");
    
    // Inicializa e testa imediatamente
    factory_reset_init();
    
    ESP_LOGI(TAG, "Testando leitura do botão:");
    for (int i = 0; i < 10; i++) {
        bool pressed = factory_reset_is_button_pressed();
        ESP_LOGI(TAG, "Botão: %s", pressed ? "PRESSIONADO" : "SOLTO");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    ESP_LOGI(TAG, "Iniciando monitoramento contínuo...");
    factory_reset_start_button_monitoring();
}

/*
 * ========================================================================
 * INSTRUÇÕES DE COMPILAÇÃO
 * ========================================================================
 * 
 * 1. Adicione esta pasta à sua pasta lib/ do projeto
 * 2. Inclua #include "factory_reset.h" no seu código
 * 3. Compile normalmente com PlatformIO ou ESP-IDF
 * 
 * Para testar:
 * - Conecte um botão entre GPIO 5 e GND (com pull-up interno)
 * - LED interno do ESP32 (GPIO 2) fornecerá feedback visual
 * - Pressione e segure botão por 3+ segundos para ativar reset
 * 
 * ========================================================================
 */