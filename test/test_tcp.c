#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "modbus_tcp_slave.h"

static const char *TAG = "TEST_TCP";

void app_main(void)
{
    // Inicializa o stack TCP/IP
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Obtém a interface WiFi
    esp_netif_t* wifi_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (wifi_netif == NULL) {
        ESP_LOGE(TAG, "Não foi possível obter interface WiFi");
        return;
    }
    
    // Configuração TCP
    modbus_tcp_config_t tcp_config = {
        .port = 502,
        .slave_id = 1,
        .max_connections = 5,
        .netif = wifi_netif,
        .auto_start = true,
        .timeout_ms = 30000
    };
    
    // Inicializa Modbus TCP
    modbus_tcp_handle_t tcp_handle;
    esp_err_t ret = modbus_tcp_slave_init(&tcp_config, &tcp_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar Modbus TCP: %s", esp_err_to_name(ret));
        return;
    }
    
    // Inicia o servidor
    ret = modbus_tcp_slave_start(tcp_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar servidor TCP: %s", esp_err_to_name(ret));
        modbus_tcp_slave_destroy(tcp_handle);
        return;
    }
    
    ESP_LOGI(TAG, "Servidor Modbus TCP iniciado na porta 502");
    
    // Define alguns valores iniciais nos registradores
    float test_value = 123.45f;
    modbus_tcp_set_holding_reg_float(tcp_handle, 0, test_value);
    
    while(1) {
        // Verifica status de conexão
        uint8_t connection_count;
        uint16_t port;
        modbus_tcp_get_connection_info(tcp_handle, &connection_count, &port);
        ESP_LOGI(TAG, "Status TCP - Porta: %d, Conexões ativas: %d", port, connection_count);
        
        // Aguarda 5 segundos
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}