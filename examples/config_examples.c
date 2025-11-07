// Exemplo de uso da nova estrutura de configuração modular
#include "config_manager.h"
#include "mqtt_client_task.h"
#include "esp_log.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "CONFIG_EXAMPLE";

void example_save_all_configs(void) {
    ESP_LOGI(TAG, "=== Exemplo: Salvando todas as configurações ===");
    
    // 1. Configuração RTU (registradores Modbus)
    ESP_LOGI(TAG, "Salvando configuração RTU...");
    save_rtu_config();
    
    // 2. Configuração Access Point
    ESP_LOGI(TAG, "Salvando configuração AP...");
    ap_config_t ap_config = {
        .ssid = "ESP32-SondaLambda",
        .username = "admin",
        .password = "senha123",
        .ip = "192.168.4.1"
    };
    save_ap_config(&ap_config);
    
    // 3. Configuração WiFi Station
    ESP_LOGI(TAG, "Salvando configuração STA...");
    sta_config_t sta_config = {
        .ssid = "MinhaRedeWiFi",
        .password = "minhasenha123"
    };
    save_sta_config(&sta_config);
    
    // 4. Configuração MQTT
    ESP_LOGI(TAG, "Salvando configuração MQTT...");
    mqtt_config_t mqtt_config = {
        .broker_url = "mqtt://test.mosquitto.org",
        .client_id = "ESP32_SondaLambda_001",
        .username = "",
        .password = "",
        .port = 1883,
        .qos = 1,
        .retain = false,
        .tls_enabled = false,
        .ca_path = "",
        .enabled = true,
        .publish_interval_ms = 2000
    };
    save_mqtt_config(&mqtt_config);
    
    // 5. Configuração de Rede
    ESP_LOGI(TAG, "Salvando configuração de rede...");
    network_config_t network_config = {
        .ip = "192.168.1.100",
        .mask = "255.255.255.0",
        .gateway = "192.168.1.1",
        .dns = "8.8.8.8"
    };
    save_network_config(&network_config);
    
    ESP_LOGI(TAG, "Todas as configurações foram salvas!");
}

void example_load_all_configs(void) {
    ESP_LOGI(TAG, "=== Exemplo: Carregando todas as configurações ===");
    
    // 1. Configuração RTU
    ESP_LOGI(TAG, "Carregando configuração RTU...");
    if (load_rtu_config() == ESP_OK) {
        ESP_LOGI(TAG, "RTU config carregada com sucesso");
    }
    
    // 2. Configuração Access Point
    ESP_LOGI(TAG, "Carregando configuração AP...");
    ap_config_t ap_config;
    if (load_ap_config(&ap_config) == ESP_OK) {
        ESP_LOGI(TAG, "AP config: SSID=%s, IP=%s", ap_config.ssid, ap_config.ip);
    }
    
    // 3. Configuração WiFi Station
    ESP_LOGI(TAG, "Carregando configuração STA...");
    sta_config_t sta_config;
    if (load_sta_config(&sta_config) == ESP_OK) {
        ESP_LOGI(TAG, "STA config: SSID=%s", sta_config.ssid);
    }
    
    // 4. Configuração MQTT
    ESP_LOGI(TAG, "Carregando configuração MQTT...");
    mqtt_config_t mqtt_config;
    if (load_mqtt_config(&mqtt_config) == ESP_OK) {
        ESP_LOGI(TAG, "MQTT config: broker=%s, enabled=%s", 
                 mqtt_config.broker_url, 
                 mqtt_config.enabled ? "true" : "false");
        
        // Aplicar no cliente MQTT
        mqtt_set_config(&mqtt_config);
    }
    
    // 5. Configuração de Rede
    ESP_LOGI(TAG, "Carregando configuração de rede...");
    network_config_t network_config;
    if (load_network_config(&network_config) == ESP_OK) {
        if (strlen(network_config.ip) > 0) {
            ESP_LOGI(TAG, "Network config: IP=%s, Gateway=%s", 
                     network_config.ip, network_config.gateway);
        } else {
            ESP_LOGI(TAG, "Network config: DHCP habilitado");
        }
    }
    
    ESP_LOGI(TAG, "Carregamento de configurações concluído!");
}

void example_modify_mqtt_config(void) {
    ESP_LOGI(TAG, "=== Exemplo: Modificando configuração MQTT ===");
    
    // Carregar configuração atual
    mqtt_config_t config;
    if (load_mqtt_config(&config) != ESP_OK) {
        ESP_LOGW(TAG, "Usando configuração MQTT padrão");
    }
    
    // Modificar algumas configurações
    strcpy(config.broker_url, "mqtt://broker.emqx.io");
    strcpy(config.client_id, "ESP32_Modified");
    config.port = 1883;
    config.qos = 2;
    config.publish_interval_ms = 5000;
    config.enabled = true;
    
    // Salvar nova configuração
    if (save_mqtt_config(&config) == ESP_OK) {
        ESP_LOGI(TAG, "Configuração MQTT modificada e salva!");
        
        // Aplicar no cliente MQTT
        mqtt_set_config(&config);
        
        if (config.enabled) {
            mqtt_restart();
        }
    }
}

void example_reset_to_defaults(void) {
    ESP_LOGI(TAG, "=== Exemplo: Reset para configurações padrão ===");
    
    // Para resetar, basta deletar os arquivos de configuração
    // O sistema carregará os valores padrão na próxima inicialização
    
    ESP_LOGW(TAG, "Para reset completo, delete os arquivos:");
    ESP_LOGW(TAG, "  /data/config/rtu_config.json");
    ESP_LOGW(TAG, "  /data/config/ap_config.json");
    ESP_LOGW(TAG, "  /data/config/sta_config.json"); 
    ESP_LOGW(TAG, "  /data/config/mqtt_config.json");
    ESP_LOGW(TAG, "  /data/config/network_config.json");
    
    // Exemplo: resetar apenas MQTT para padrão
    mqtt_config_t default_mqtt = {
        .broker_url = "mqtt://broker.hivemq.com",
        .client_id = "ESP32_SondaLambda",
        .username = "",
        .password = "",
        .port = 1883,
        .qos = 1,
        .retain = false,
        .tls_enabled = false,
        .ca_path = "",
        .enabled = true,
        .publish_interval_ms = 1000
    };
    
    save_mqtt_config(&default_mqtt);
    ESP_LOGI(TAG, "MQTT resetado para configuração padrão");
}

// Função para chamar nos exemplos
void run_config_examples(void) {
    ESP_LOGI(TAG, "Executando exemplos da nova estrutura de configuração...");
    
    // Exemplo 1: Salvar configurações
    example_save_all_configs();
    
    // Pequena pausa
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Exemplo 2: Carregar configurações
    example_load_all_configs();
    
    // Exemplo 3: Modificar MQTT
    example_modify_mqtt_config();
    
    // Exemplo 4: Reset (comentado para não apagar configs reais)
    // example_reset_to_defaults();
    
    ESP_LOGI(TAG, "Exemplos concluídos!");
}