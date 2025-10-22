#include <stdio.h>
#include <stdint.h>
#include "esp_modbus_slave.h"
#include "modbus_params.h"
#include "modbus_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "modbus_slave_task.h"

#include "config_manager.h"
// Adicionados para uso de Wi-Fi, cJSON e funções de string
#include "wifi_manager.h"
#include "cJSON.h"
#include <string.h>
// Incluído para acesso às variáveis globais da sonda
#include "globalvar.h"

static const char *TAG = "MODBUS_SLAVE";

// Definição de MB_PORT_TCP caso não exista
#ifndef MB_PORT_TCP
#define MB_PORT_TCP 502
#endif


// Função para inicializar os registradores Modbus com valores padrão
static void setup_registers(void) {

    discrete_reg_params.discrete_input0 = 1;
    discrete_reg_params.discrete_input1 = 0;
    holding_reg_params.holding_data0 = 123;
    holding_reg_params.holding_data1 = 321;
    coil_reg_params.coils_port0 = 0x00;

    holding_reg1000_params.reg1000[baudrate] = 9600;
    holding_reg1000_params.reg1000[endereco] = 1;
    holding_reg1000_params.reg1000[paridade] = 0; // No parity

    load_config(); // Carrega a configuração salva, se existir

    reg2000[dataValue] = 2100; // Inicializa o primeiro registrador de dados

    reg3000[maxDac] = 3100; // Inicializa o primeiro registrador de dados
    reg3000[minDac] = 616; // Inicializa o segundo registrador de

    reg4000[lambdaValue] = 4100; // Inicializa o primeiro registrador de dados
    reg4000[lambdaRef] = 416; // Inicializa o segundo registrador de dados
    reg4000[heatValue] = 5100; // Inicializa o terceiro registrador de dados
    reg4000[heatRef] = 516; // Inicializa o quarto registrador de dados
    reg4000[output_mb] = 6100; // Inicializa o quinto registrador de dados
    reg4000[PROBE_DEMAGED] = 616; // Inicializa o sexto registrador de dados
    reg4000[PROBE_TEMP_OUT_OF_RANGE] = 7100; // Inicializa o sétimo registrador de dados
    reg4000[COMPRESSOR_FAIL] = 716; // Inicializa o oitavo registrador de dados

    reg5000[teste1] = 5100; // Inicializa o primeiro registrador de dados
    reg5000[teste2] = 516; // Inicializa o segundo registrador de dados
    reg5000[teste3] = 6100; // Inicializa o terceiro registrador de dados
    reg5000[teste4] = 616; // Inicializa o quarto registrador de dados

    reg6000[maxDac0] = 6100; // Inicializa o primeiro registrador de dados
    reg6000[forcaValorDAC] = 616; // Inicializa o segundo registrador de
    reg6000[nada] = 7100; // Inicializa o terceiro registrador de dados
    reg6000[dACGain0] = 716; // Inicializa o quarto registrador de
    reg6000[dACOffset0] = 8100; // Inicializa o quinto registrador de dados

    reg9000[valorZero] = 9000; // Inicializa o primeiro registrador de dados
    reg9000[valorUm] = 9010; // Inicializa o segundo registrador de dados
    reg9000[firmVerHi] = 9020; // Inicializa o terceiro
    reg9000[firmVerLo] = 9030; // Inicializa o quarto
    reg9000[valorQuatro] = 9040; // Inicializa o quinto registrador de dados
    reg9000[valorCinco] = 9050; // Inicializa o sexto   registrador de dados
    reg9000[lotnum0] = 9060; // Inicializa o sétimo registrador de dados
    reg9000[lotnum1] = 9070; // Inicializa o oitavo registrador de dados
    reg9000[lotnum2] = 9080; // Inicializa o nono registrador de dados
    reg9000[lotnum3] = 9090; // Inicializa o décimo registrador de dados  
    reg9000[lotnum4] = 9100; // Inicializa o décimo primeiro registrador de dados
    reg9000[lotnum5] = 9110; // Inicializa o décimo segundo registrador de dados
    reg9000[wafnum] = 9120; // Inicializa o décimo terceiro registrador de dados
    reg9000[coordx0] = 9130; // Inicializa o décimo quarto registrador de dados
    reg9000[coordx1] = 9140; // Inicializa o décimo quinto registrador de dados
    reg9000[coordy0] = 9150; // Inicializa o décimo sexto registrador de dados
    reg9000[coordy1] = 9160; // Inicializa o décimo sétimo registrador de dados
    reg9000[valor17] = 9170; // Inicializa o décimo oitavo registrador de dados
    reg9000[valor18] = 9170; // Inicializa o décimo oitavo registrador de dados
    reg9000[valor19] = 9170; // Inicializa o décimo oitavo registrador de dados



}

void modbus_slave_task(void *pvParameters) {
    mb_param_info_t reg_info;
    mb_communication_info_t comm_info;
    mb_register_area_descriptor_t reg_area;

    void* mbc_slave_handler = NULL;

    ESP_LOGI(TAG, "Modbus Slave Task starting...");

    // // Inicializa os registradores antes de registrar
    setup_registers();

    // Lê modo do Modbus de config.json
    char modbus_mode[8] = "rtu";
    {
        FILE *f = fopen("/spiffs/config.json", "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            rewind(f);
            char *data = malloc(size + 1);
            if (data) {
                fread(data, 1, size, f);
                data[size] = '\0';
                cJSON *root = cJSON_Parse(data);
                if (root) {
                    cJSON *mode_item = cJSON_GetObjectItem(root, "modbus_mode");
                    if (mode_item && cJSON_IsString(mode_item)) {
                        strncpy(modbus_mode, mode_item->valuestring, sizeof(modbus_mode)-1);
                        modbus_mode[sizeof(modbus_mode)-1] = '\0';
                    }
                    cJSON_Delete(root);
                }
                free(data);
            }
            fclose(f);
        }
    }

    wifi_status_t wifi_st = wifi_get_status();

    if (strcmp(modbus_mode, "tcp") == 0) {
        // Only start TCP if Wi-Fi STA is connected and has IP
        if (wifi_st.is_connected && wifi_st.ip_address[0] != '\0') {
            ESP_LOGI(TAG, "Inicializando Modbus TCP (Wi-Fi conectado, IP: %s)", wifi_st.ip_address);
            ESP_ERROR_CHECK(mbc_slave_init(MB_PORT_TCP, &mbc_slave_handler));
            comm_info.mode = MB_MODE_TCP;
            comm_info.slave_addr = MB_SLAVE_ADDR;
            comm_info.port = 502; // or read from config.json
            comm_info.baudrate = 0;
            comm_info.parity = MB_PARITY_NONE;
            ESP_ERROR_CHECK(mbc_slave_setup(&comm_info));
        } else {
            ESP_LOGW(TAG, "Modo Modbus TCP selecionado, mas Wi-Fi não está conectado ou sem IP. Modbus TCP não será iniciado.");
            vTaskDelete(NULL);
            return;
        }
    } else if (strcmp(modbus_mode, "ambos") == 0) {
        // Prefer TCP when Wi-Fi available, otherwise fall back to RTU
        if (wifi_st.is_connected && wifi_st.ip_address[0] != '\0') {
            ESP_LOGI(TAG, "Modo AMBOS: Inicializando Modbus TCP (Wi-Fi conectado, IP: %s)", wifi_st.ip_address);
            ESP_ERROR_CHECK(mbc_slave_init(MB_PORT_TCP, &mbc_slave_handler));
            comm_info.mode = MB_MODE_TCP;
            comm_info.slave_addr = MB_SLAVE_ADDR;
            comm_info.port = 502;
            comm_info.baudrate = 0;
            comm_info.parity = MB_PARITY_NONE;
            ESP_ERROR_CHECK(mbc_slave_setup(&comm_info));
        } else {
            ESP_LOGI(TAG, "Modo AMBOS: Wi-Fi indisponível, iniciando Modbus RTU (Serial)");
            ESP_ERROR_CHECK(mbc_slave_init(MB_PORT_SERIAL_SLAVE, &mbc_slave_handler));
            comm_info.mode = MB_MODE_RTU;
            comm_info.slave_addr = MB_SLAVE_ADDR;
            comm_info.port = MB_PORT_NUM;
            comm_info.baudrate = MB_DEV_SPEED;
            comm_info.parity = MB_PARITY_NONE;
            ESP_ERROR_CHECK(mbc_slave_setup(&comm_info));
        }
    } else {
        ESP_LOGI(TAG, "Inicializando Modbus RTU (Serial)");
        ESP_ERROR_CHECK(mbc_slave_init(MB_PORT_SERIAL_SLAVE, &mbc_slave_handler));
        comm_info.mode = MB_MODE_RTU;
        comm_info.slave_addr = MB_SLAVE_ADDR;
        comm_info.port = MB_PORT_NUM;
        comm_info.baudrate = MB_DEV_SPEED;
        comm_info.parity = MB_PARITY_NONE;
        ESP_ERROR_CHECK(mbc_slave_setup(&comm_info));
    }
    ESP_LOGI(TAG, "Modbus handler initialized: %p", mbc_slave_handler);
    ESP_LOGI(TAG, "Modbus communication setup done.");
    ESP_LOGI(TAG, "Meu log1: %d",comm_info.slave_addr);
    ESP_LOGI(TAG, "Meu log2: %d",comm_info.port);
    ESP_LOGI(TAG, "Meu log3: %ld",(long)comm_info.baudrate);
    ESP_LOGI(TAG, "Meu log4: %d",comm_info.parity);
    // // // Configura área de registradores
    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = 1000;
    reg_area.address = (void*)&holding_reg1000_params.reg1000;
    reg_area.size = sizeof(holding_reg1000_params);
    ESP_ERROR_CHECK(mbc_slave_set_descriptor(reg_area));
    ESP_LOGI(TAG, "Holding registers descriptor set.");

    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = REG_DATA_START;
    reg_area.address = (void*)&reg2000;
    reg_area.size = sizeof(reg2000);
    ESP_ERROR_CHECK(mbc_slave_set_descriptor(reg_area));
    ESP_LOGI(TAG, "Holding registers descriptor set.");

    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = REG_3000_START;
    reg_area.address = (void*)&reg3000;
    reg_area.size = sizeof(reg3000);
    ESP_ERROR_CHECK(mbc_slave_set_descriptor(reg_area));
    ESP_LOGI(TAG, "Holding registers descriptor set.");

    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = REG_4000_START;
    reg_area.address = (void*)&reg4000;
    reg_area.size = sizeof(reg4000);
    ESP_ERROR_CHECK(mbc_slave_set_descriptor(reg_area));
    ESP_LOGI(TAG, "Holding registers descriptor set.");

    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = REG_5000_START;
    reg_area.address = (void*)&reg5000;
    reg_area.size = sizeof(reg5000);
    ESP_ERROR_CHECK(mbc_slave_set_descriptor(reg_area));
    ESP_LOGI(TAG, "Holding registers descriptor set.");

    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = REG_6000_START;
    reg_area.address = (void*)&reg6000;
    reg_area.size = sizeof(reg6000);
    ESP_ERROR_CHECK(mbc_slave_set_descriptor(reg_area));
    ESP_LOGI(TAG, "Holding registers descriptor set.");

    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = REG_7000_START;
    reg_area.address = (void*)&reg7000;
    reg_area.size = sizeof(reg7000);
    ESP_ERROR_CHECK(mbc_slave_set_descriptor(reg_area));
    ESP_LOGI(TAG, "Holding registers descriptor set.");

    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = REG_8000_START;
    reg_area.address = (void*)&reg8000;
    reg_area.size = sizeof(reg8000);
    ESP_ERROR_CHECK(mbc_slave_set_descriptor(reg_area));
    ESP_LOGI(TAG, "Holding registers descriptor set.");

    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = REG_UNITSPECS_START;
    reg_area.address = (void*)&reg9000;
    reg_area.size = sizeof(reg9000);
    ESP_ERROR_CHECK(mbc_slave_set_descriptor(reg_area));

    // // // Inicia Modbus
    ESP_ERROR_CHECK(mbc_slave_start());
    ESP_LOGI(TAG, "Modbus slave started.");

    // Set UART pin numbers
    ESP_ERROR_CHECK(uart_set_pin(MB_PORT_NUM, CONFIG_MB_UART_TXD,
                            CONFIG_MB_UART_RXD, CONFIG_MB_UART_RTS,
                            UART_PIN_NO_CHANGE));

    // Set UART driver mode to Half Duplex
    ESP_ERROR_CHECK(uart_set_mode(MB_PORT_NUM, UART_MODE_RS485_HALF_DUPLEX));

    ESP_LOGI(TAG, "Modbus slave stack initialized.");
    ESP_LOGI(TAG, "Start modbus test...");

    // Loop principal do Modbus
    for (;;) {
        
        // ========== SINCRONIZAÇÃO DOS DADOS DA SONDA COM REGISTRADORES MODBUS ==========
        // Atualiza registradores com dados reais da MCT Task
        extern volatile int16_t sonda_heatValue_sync;
        extern volatile int16_t sonda_lambdaValue_sync;
        extern volatile int16_t sonda_heatRef_sync;
        extern volatile int16_t sonda_lambdaRef_sync;
        extern volatile uint16_t sonda_o2Percent_sync;
        extern volatile uint32_t sonda_output_sync;
        
        // Sincroniza reg4000 com dados atuais da sonda
        reg4000[lambdaValue] = sonda_lambdaValue_sync;
        reg4000[lambdaRef] = sonda_lambdaRef_sync;
        reg4000[heatValue] = sonda_heatValue_sync;
        reg4000[heatRef] = sonda_heatRef_sync;
        reg4000[output_mb] = (uint16_t)(sonda_output_sync & 0xFFFF); // Trunca para 16 bits
        
        // Adiciona O2% no registrador disponível (usando um dos campos de erro como exemplo)
        reg4000[PROBE_DEMAGED] = sonda_o2Percent_sync; // O2% nos registradores de diagnóstico

        (void)mbc_slave_check_event(MB_READ_WRITE_MASK);

        // // Obtém informações de eventos (com timeout seguro)
        ESP_ERROR_CHECK_WITHOUT_ABORT(mbc_slave_get_param_info(&reg_info, MB_PAR_INFO_GET_TOUT));

        if (reg_info.type & (MB_EVENT_HOLDING_REG_WR | MB_EVENT_HOLDING_REG_RD)) {
            ESP_LOGI(TAG, "HOLDING REG EVENT: ADDR=%u TYPE=%u", 
                     (unsigned)reg_info.mb_offset, (unsigned)reg_info.type);
            if (reg_info.type & MB_EVENT_HOLDING_REG_WR) {
                if (reg_info.mb_offset == 1000 + baudrate ||
                    reg_info.mb_offset == 1000 + endereco ||
                    reg_info.mb_offset == 1000 + paridade) {
                    save_config();
    }
}

                     
                     
        }

        // Pequeno delay para não travar o scheduler
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }

    // Nunca deve chegar aqui
    ESP_LOGI(TAG, "Modbus Slave Task terminated.");
    vTaskDelete(NULL);
}
