/*
 * ========================================================================
 * INTEGRAÇÃO DA LIBRARY FACTORY_RESET NO PROJETO EXISTENTE
 * ========================================================================
 * 
 * Este arquivo demonstra como integrar a nova biblioteca factory_reset
 * no seu projeto existente, substituindo o código disperso.
 * 
 * MUDANÇAS NECESSÁRIAS:
 * 1. Incluir a biblioteca no main.c
 * 2. Substituir handler do webserver
 * 3. Atualizar event_bus.h (se necessário)
 * 4. Remover código duplicado
 * 
 * ========================================================================
 */

// EXEMPLO DE INTEGRAÇÃO NO MAIN.C
// ================================

#include "factory_reset.h"

// Adicionar na função app_main(), após inicialização básica:
void app_main(void) {
    // ... código existente ...
    
    // ========== NOVO: INICIALIZAÇÃO FACTORY RESET ==========
    ESP_LOGI(TAG, "🔧 Inicializando sistema de Factory Reset...");
    
    esp_err_t factory_ret = factory_reset_init();
    if (factory_ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ ERRO: Falha ao inicializar Factory Reset!");
        // Sistema pode continuar sem factory reset
    } else {
        ESP_LOGI(TAG, "✅ Factory Reset inicializado com sucesso");
        
        // Inicia monitoramento do botão (GPIO 4)
        factory_reset_start_button_monitoring();
        ESP_LOGI(TAG, "📱 Monitoramento do botão de reset ativo (GPIO 5, 3s)");
    }
    
    // ... resto do código existente ...
}

// EXEMPLO DE INTEGRAÇÃO NO WEBSERVER.C  
// =====================================

// SUBSTITUIR o handler existente:
/*
// CÓDIGO ANTIGO - REMOVER:
static esp_err_t factory_reset_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Factory reset requested via web");
    // ... código duplicado ...
}
*/

// CÓDIGO NOVO - USAR:
#include "factory_reset.h"

// Função wrapper (opcional) para manter compatibilidade
static esp_err_t factory_reset_post_handler(httpd_req_t *req) {
    // Delega para a biblioteca
    return factory_reset_web_handler(req);
}

// Ou usar diretamente no registro:
httpd_uri_t reset_uri = {
    .uri = "/factory_reset",
    .method = HTTP_POST,
    .handler = factory_reset_web_handler  // ← Usar diretamente da biblioteca
};

// INTEGRAÇÃO COM EVENT_BUS (event_bus.h)
// =======================================

// A biblioteca detecta automaticamente se event_bus.h existe
// Se as funções eventbus_factory_reset_start() e eventbus_factory_reset_complete() 
// estiverem disponíveis, serão usadas automaticamente.

// Caso queira forçar integração, pode chamar manualmente:
void custom_factory_reset_callback(factory_reset_type_t type, factory_reset_state_t state) {
    if (state == FACTORY_RESET_STATE_EXECUTING) {
        // Notifica máquina de estados manualmente se necessário
        #ifdef HAS_EVENT_BUS
        eventbus_factory_reset_start();
        #endif
    }
    else if (state == FACTORY_RESET_STATE_COMPLETED) {
        #ifdef HAS_EVENT_BUS  
        eventbus_factory_reset_complete();
        #endif
    }
}

// Registra callback customizado:
factory_reset_register_callback(custom_factory_reset_callback);

// CONFIGURAÇÃO PERSONALIZADA PARA SEU PROJETO
// ============================================

void setup_custom_factory_reset(void) {
    // Configuração que corresponde ao seu hardware atual
    factory_reset_config_t project_config = {
        .button_gpio = GPIO_NUM_5,              // Atualizado para GPIO 5
        .led_gpio = GPIO_NUM_2,                 // Como definido no seu main.c  
        .press_time_ms = 3000,                  // 3 segundos como no seu código
        .debounce_time_ms = 50,                 // Debounce padrão
        .enable_button_monitoring = true,       // Habilita botão físico
        .enable_led_feedback = true             // Habilita feedback LED
    };
    
    esp_err_t ret = factory_reset_init_with_config(&project_config);
    if (ret == ESP_OK) {
        factory_reset_start_button_monitoring();
        ESP_LOGI("SETUP", "Factory Reset configurado conforme projeto");
    }
}

// COMPATIBILIDADE COM CÓDIGO EXISTENTE
// ====================================

// Se quiser manter compatibilidade total, pode criar wrappers:

esp_err_t eventbus_factory_reset_start(void) {
    return factory_reset_notify_start();
}

esp_err_t eventbus_factory_reset_complete(void) {
    return factory_reset_notify_complete();
}

// LIMPEZA DE CÓDIGO ANTIGO
// =========================

/*
REMOVER DO MAIN.C:
- #define RESET_BUTTON_GPIO GPIO_NUM_4 (agora na biblioteca)
- #define RESET_BUTTON_PRESS_TIME_MS 3000 (agora na biblioteca)  
- #define RESET_LED_GPIO GPIO_NUM_2 (agora na biblioteca)
- volatile bool reset_pending = false; (gerenciado pela biblioteca)
- Função factory_reset() comentada (agora na biblioteca)
- Estados e eventos específicos de factory reset (se quiser)

REMOVER DO WEBSERVER.C:
- factory_reset_post_handler() (usar factory_reset_web_handler)
- Código duplicado de NVS erase e file removal

OPCIONAL - MANTER NO MAIN.C:
- Estados da máquina de estados relacionados a factory reset
- Eventos EVENT_FACTORY_RESET_* (a biblioteca integra automaticamente)
- Timeout TIMEOUT_FACTORY_RESET_MS (a biblioteca tem próprio timeout)
*/

// EXEMPLO COMPLETO DE MIGRAÇÃO
// =============================

void migrate_to_factory_reset_library(void) {
    ESP_LOGI("MIGRATE", "=== MIGRANDO PARA FACTORY RESET LIBRARY ===");
    
    // 1. Substitui inicialização manual por biblioteca
    factory_reset_init();
    
    // 2. Substitui task manual por monitoramento da biblioteca  
    factory_reset_start_button_monitoring();
    
    // 3. Mantém integração com máquina de estados se necessário
    // (automática se event_bus.h disponível)
    
    // 4. Webserver usa handler da biblioteca
    // (modificar no código de registro do servidor)
    
    ESP_LOGI("MIGRATE", "✅ Migração concluída - factory reset agora é biblioteca");
    ESP_LOGI("MIGRATE", "📖 Funcionalidades:");
    ESP_LOGI("MIGRATE", "   - Botão físico: GPIO 5 (3s)");
    ESP_LOGI("MIGRATE", "   - LED feedback: GPIO 2");
    ESP_LOGI("MIGRATE", "   - Web endpoint: /factory_reset");
    ESP_LOGI("MIGRATE", "   - Integração automática com event_bus");
    ESP_LOGI("MIGRATE", "   - Thread-safe e assíncrono");
}