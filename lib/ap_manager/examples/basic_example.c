#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "nvs_flash.h"

// Incluir a biblioteca AP Manager
#include "ap_manager.h"

static const char *TAG = "AP_EXAMPLE";

// Event group para sincronização
static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;
const int AP_STARTED_BIT = BIT1;

// Callback para eventos do AP Manager
void ap_manager_event_callback(ap_manager_event_id_t event, void *event_data) {
    ap_manager_status_t *status = (ap_manager_status_t *)event_data;
    
    switch (event) {
        case AP_MANAGER_EVENT_AP_STARTED:
            ESP_LOGI(TAG, "✅ Access Point iniciado!");
            xEventGroupSetBits(wifi_event_group, AP_STARTED_BIT);
            break;
            
        case AP_MANAGER_EVENT_AP_STOPPED:
            ESP_LOGI(TAG, "⏹️ Access Point parado");
            xEventGroupClearBits(wifi_event_group, AP_STARTED_BIT);
            break;
            
        case AP_MANAGER_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "📶 Conectado ao WiFi: %s", status->current_ssid);
            break;
            
        case AP_MANAGER_EVENT_STA_GOT_IP:
            ESP_LOGI(TAG, "🌐 IP obtido: %s", status->ip_address);
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
            break;
            
        case AP_MANAGER_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG, "❌ Desconectado do WiFi");
            xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
            break;
            
        case AP_MANAGER_EVENT_SCAN_COMPLETED:
            ESP_LOGI(TAG, "🔍 Scan concluído. Redes encontradas: %d", *(uint16_t*)event_data);
            break;
            
        case AP_MANAGER_EVENT_CONFIG_SAVED:
            ESP_LOGI(TAG, "💾 Configuração salva!");
            break;
            
        default:
            ESP_LOGI(TAG, "📡 Evento AP Manager: %d", event);
            break;
    }
}

// Task para demonstrar funcionalidades
void demo_task(void *pvParameters) {
    ESP_LOGI(TAG, "Iniciando demonstração do AP Manager");
    
    // Aguardar AP inicializar
    xEventGroupWaitBits(wifi_event_group, AP_STARTED_BIT, false, true, portMAX_DELAY);
    
    while (1) {
        // Obter status atual
        ap_manager_status_t status = ap_manager_get_status();
        
        ESP_LOGI(TAG, "=== Status Atual ===");
        ESP_LOGI(TAG, "AP Ativo: %s", status.ap_active ? "Sim" : "Não");
        ESP_LOGI(TAG, "STA Conectado: %s", status.is_connected ? "Sim" : "Não");
        
        if (status.is_connected) {
            ESP_LOGI(TAG, "SSID Conectado: %s", status.current_ssid);
            ESP_LOGI(TAG, "IP: %s", status.ip_address);
            ESP_LOGI(TAG, "RSSI: %d dBm", status.rssi);
        }
        
        ESP_LOGI(TAG, "Status: %s", status.status_message);
        ESP_LOGI(TAG, "==================");
        
        // Executar scan a cada 30 segundos se não estiver em progresso
        if (!ap_manager_is_scan_in_progress()) {
            ESP_LOGI(TAG, "Iniciando scan de redes WiFi...");
            esp_err_t ret = ap_manager_start_scan();
            if (ret == ESP_OK) {
                // Aguardar scan completar (máximo 10 segundos)
                vTaskDelay(pdMS_TO_TICKS(10000));
                
                // Obter resultados do scan
                wifi_ap_record_t scan_records[10];
                uint16_t scan_count;
                
                ret = ap_manager_get_scan_results(scan_records, 10, &scan_count);
                if (ret == ESP_OK && scan_count > 0) {
                    ESP_LOGI(TAG, "=== Redes Encontradas ===");
                    for (int i = 0; i < scan_count; i++) {
                        ESP_LOGI(TAG, "%d. SSID: %s, RSSI: %d, Canal: %d", 
                               i+1, scan_records[i].ssid, scan_records[i].rssi, scan_records[i].primary);
                    }
                    ESP_LOGI(TAG, "========================");
                }
            } else {
                ESP_LOGW(TAG, "Falha ao iniciar scan: %s", esp_err_to_name(ret));
            }
        }
        
        // Aguardar 30 segundos antes do próximo ciclo
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

// Função para configurar AP personalizado
void configure_custom_ap(void) {
    ESP_LOGI(TAG, "Configurando Access Point personalizado");
    
    ap_manager_config_t ap_config;
    
    // Configurar AP personalizado
    strncpy(ap_config.ssid, "MeuESP32_AP", sizeof(ap_config.ssid) - 1);
    strncpy(ap_config.password, "minhasenha123", sizeof(ap_config.password) - 1);
    strncpy(ap_config.ip, "192.168.10.1", sizeof(ap_config.ip) - 1);
    ap_config.channel = 6;
    ap_config.max_connections = 4;
    ap_config.hidden = false;
    
    esp_err_t ret = ap_manager_set_ap_config(&ap_config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ Configuração AP salva com sucesso!");
    } else {
        ESP_LOGE(TAG, "❌ Falha ao salvar configuração AP: %s", esp_err_to_name(ret));
    }
}

// Função para conectar a uma rede WiFi específica
void connect_to_wifi(const char *ssid, const char *password) {
    ESP_LOGI(TAG, "Tentando conectar ao WiFi: %s", ssid);
    
    esp_err_t ret = ap_manager_connect_sta(ssid, password);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Conexão WiFi iniciada...");
        
        // Aguardar conexão (máximo 30 segundos)
        EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, 
                                              false, true, pdMS_TO_TICKS(30000));
        
        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "✅ Conectado com sucesso!");
            
            // Configurar auto-switch para STA-only após 60 segundos
            ap_manager_auto_switch_to_sta(60000);
        } else {
            ESP_LOGW(TAG, "⏰ Timeout - não foi possível conectar");
        }
    } else {
        ESP_LOGE(TAG, "❌ Falha ao iniciar conexão: %s", esp_err_to_name(ret));
    }
}

void app_main(void) {
    // Inicializar NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Criar event group
    wifi_event_group = xEventGroupCreate();
    
    ESP_LOGI(TAG, "=== Exemplo AP Manager para ESP32 ===");
    ESP_LOGI(TAG, "Inicializando AP Manager...");
    
    // Inicializar AP Manager
    ret = ap_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar AP Manager: %s", esp_err_to_name(ret));
        return;
    }
    
    // Registrar callback para eventos
    ap_manager_set_event_callback(ap_manager_event_callback);
    
    // Configurar AP personalizado (opcional)
    // Descomente a linha abaixo para usar configuração customizada
    // configure_custom_ap();
    
    // Iniciar Access Point
    ret = ap_manager_start_ap();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar Access Point: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "✅ AP Manager iniciado com sucesso!");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🌐 Access Point ativo");
    ESP_LOGI(TAG, "📱 Conecte-se ao WiFi para configurar");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Para conectar automaticamente a uma rede WiFi,");
    ESP_LOGI(TAG, "descomente a função connect_to_wifi() abaixo:");
    ESP_LOGI(TAG, "");
    
    // Exemplo de conexão automática a uma rede WiFi
    // Descomente e configure com suas credenciais WiFi
    // connect_to_wifi("SuaRedeWiFi", "SuaSenhaWiFi");
    
    // Criar task de demonstração
    xTaskCreate(&demo_task, "demo_task", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "=== Sistema pronto ===");
}