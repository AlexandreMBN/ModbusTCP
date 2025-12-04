/*
 * Exemplo de integração da biblioteca AP Manager no PlatformIO
 * 
 * Como usar:
 * 1. A biblioteca já está em lib/ap_manager/
 * 2. Incluir o header: #include <ap_manager.h>
 * 3. Usar as funções normalmente
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

// Incluir a biblioteca AP Manager (automaticamente detectada pelo PlatformIO)
#include <ap_manager.h>

static const char *TAG = "MAIN";

void app_main(void) {
    // Inicializar NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_LOGI(TAG, "=== Exemplo AP Manager - PlatformIO ===");
    
    // Inicializar AP Manager
    ret = ap_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar AP Manager: %s", esp_err_to_name(ret));
        return;
    }
    
    // Configurar AP personalizado (opcional)
    ap_manager_config_t ap_config = {
        .ssid = "MeuESP32_PIO",
        .password = "senha1234",
        .ip = "192.168.4.1",
        .channel = 1,
        .max_connections = 4,
        .hidden = false
    };
    
    // Salvar configuração
    ret = ap_manager_set_ap_config(&ap_config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Configuração AP salva");
    }
    
    // Iniciar Access Point
    ret = ap_manager_start_ap();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ Access Point iniciado com sucesso!");
        ESP_LOGI(TAG, "📱 Conecte-se ao WiFi: %s", ap_config.ssid);
        ESP_LOGI(TAG, "🌐 Acesse: http://%s", ap_config.ip);
    } else {
        ESP_LOGE(TAG, "❌ Falha ao iniciar AP: %s", esp_err_to_name(ret));
    }
    
    // Loop principal
    while (1) {
        ap_manager_status_t status = ap_manager_get_status();
        ESP_LOGI(TAG, "Status: AP=%s, STA=%s, IP=%s", 
                status.ap_active ? "ON" : "OFF",
                status.is_connected ? "CONECTADO" : "DESCONECTADO",
                status.ip_address);
        
        vTaskDelay(pdMS_TO_TICKS(10000)); // Log a cada 10 segundos
    }
}