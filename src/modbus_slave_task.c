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

// ========== NOVO: SISTEMA DE FILAS ==========
#include "queue_manager.h"  // Para receber dados via filas

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

    // Valores padrão iniciais para todos os registradores
    ESP_LOGI(TAG, "ANTES de load_config(): reg2000[dataValue] = %d", reg2000[dataValue]);
    reg2000[dataValue] = 2100; // padrão

    reg3000[maxDac] = 3100;
    reg3000[minDac] = 616;

    reg4000[lambdaValue] = 4100;
    reg4000[lambdaRef] = 416;
    reg4000[heatValue] = 5100;
    reg4000[heatRef] = 516;
    reg4000[output_mb] = 6100;
    reg4000[PROBE_DEMAGED] = 0;
    reg4000[PROBE_TEMP_OUT_OF_RANGE] = 0;
    reg4000[COMPRESSOR_FAIL] = 0;

    reg5000[teste1] = 5100;
    reg5000[teste2] = 516;
    reg5000[teste3] = 6100;
    reg5000[teste4] = 616;

    reg6000[maxDac0] = 6100;
    reg6000[forcaValorDAC] = 616;
    reg6000[nada] = 7100;
    reg6000[dACGain0] = 716;
    reg6000[dACOffset0] = 8100;

    reg9000[valorZero] = 9000;
    reg9000[valorUm] = 9010;
    reg9000[firmVerHi] = 9020;
    reg9000[firmVerLo] = 9030;
    reg9000[valorQuatro] = 9040;
    reg9000[valorCinco] = 9050;
    reg9000[lotnum0] = 9060;
    reg9000[lotnum1] = 9070;
    reg9000[lotnum2] = 9080;
    reg9000[lotnum3] = 9090;
    reg9000[lotnum4] = 9100;
    reg9000[lotnum5] = 9110;
    reg9000[wafnum] = 9120;
    reg9000[coordx0] = 9130;
    reg9000[coordx1] = 9140;
    reg9000[coordy0] = 9150;
    reg9000[coordy1] = 9160;
    reg9000[valor17] = 9170;
    reg9000[valor18] = 9170;
    reg9000[valor19] = 9170;

    // Carrega a configuração salva e, se sucesso, sobrescreve defaults
    esp_err_t config_result = load_config();
    ESP_LOGI(TAG, "DEPOIS de load_config(): reg2000[dataValue] = %d", reg2000[dataValue]);

    // Se falhou, mantém defaults já definidos
    if (config_result != ESP_OK) {
        ESP_LOGW(TAG, "Config não carregada, mantendo valores padrão");
    }



}

void modbus_slave_task(void *pvParameters) {
    mb_param_info_t reg_info;
    mb_communication_info_t comm_info;
    mb_register_area_descriptor_t reg_area;

    void* mbc_slave_handler = NULL;

    ESP_LOGI(TAG, "Modbus Slave Task starting...");

    // ========== INICIALIZA MUTEX DE PROTEÇÃO DOS REGISTRADORES ==========
    if (!modbus_mutex_init()) {
        ESP_LOGE(TAG, "FATAL: Falha ao criar mutex de registradores!");
        vTaskDelete(NULL);
        return;
    }

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
        
        // ========== NOVO: RECEPÇÃO VIA FILA (MÉTODO MODERNO) ==========
        // OTIMIZAÇÃO: Tenta processar TODAS as mensagens disponíveis na fila
        // para evitar acúmulo e overflow
        o2_queue_msg_t o2_msg;
        esp_err_t queue_result = ESP_ERR_TIMEOUT;
        int messages_processed = 0;
        
        // DEBUG: Verifica quantas mensagens estão na fila ANTES de processar
        uint32_t pending_before = queue_get_o2_pending_count();
        
        // 🚨 FORÇA LOG SEMPRE para debug
        ESP_LOGI(TAG, "MODBUS LOOP ativo: %lu msgs na fila", (unsigned long)pending_before);
        
        if (pending_before > 0) {
            ESP_LOGI(TAG, "Processando %lu mensagens da fila...", (unsigned long)pending_before);
        }
        
        // Processa até 5 mensagens por iteração ou até a fila ficar vazia
        while (messages_processed < 5) {
            queue_result = queue_receive_o2_data(&o2_msg);
            if (queue_result != ESP_OK) {
                ESP_LOGI(TAG, "Fila vazia ou erro: %s", esp_err_to_name(queue_result));
                break; // Fila vazia
            }
            messages_processed++;
            ESP_LOGI(TAG, "Mensagem %d processada: O2=%d%% (timestamp=%lu)", 
                     messages_processed, o2_msg.o2_percent, o2_msg.timestamp);
        }
        
        if (messages_processed > 0) {
            // DADOS RECEBIDOS VIA FILA - Atualiza registrador com dados frescos (último valor)
            ESP_LOGI(TAG, "SUCESSO: Processou %d msgs O2: %d%% (timestamp=%lu, válido=%d)", 
                     messages_processed, o2_msg.o2_percent, o2_msg.timestamp, o2_msg.data_valid);
            
            if (o2_msg.data_valid) {
                // ========== PROTEÇÃO MUTEX: Escrita em reg2000 ==========
                if (modbus_lock_registers(100)) {
                    // Atualiza registrador 2000 (dados principais) com valor da fila
                    reg2000[dataValue] = o2_msg.o2_percent;
                    modbus_unlock_registers();
                } else {
                    ESP_LOGW(TAG, "Timeout ao tentar lock para atualizar reg2000");
                }
                
                // COMPATIBILIDADE: Também atualiza a variável global
                extern volatile uint16_t sonda_o2Percent_sync;
                sonda_o2Percent_sync = o2_msg.o2_percent;
                
                ESP_LOGD(TAG, "Registrador 2000 atualizado via fila: %d", o2_msg.o2_percent);
            }
            
            // Log de estatísticas da fila a cada 10 iterações (mais frequente para debug)
            static int total_processed = 0;
            total_processed += messages_processed;
            if (total_processed >= 10) {
                uint32_t pending = queue_get_o2_pending_count();
                ESP_LOGI(TAG, "Fila O2 Stats: %lu msgs pendentes, %d processadas nesta iteração", 
                         (unsigned long)pending, messages_processed);
                
                // 🛠️ EMERGÊNCIA: Se fila está muito cheia (>40), limpa para evitar travamento
                if (pending > 40) {
                    ESP_LOGW(TAG, "EMERGÊNCIA: Fila muito cheia (%lu), limpando!", (unsigned long)pending);
                    queue_clear_o2_data();
                }
                
                total_processed = 0;
            }
        } else {
            // ⏰ Nenhum dado novo na fila - continua com o método antigo (fallback)
            ESP_LOGD(TAG, "📦 Fila O2 vazia, usando variáveis globais (fallback)");
        }
        
        // ========== SINCRONIZAÇÃO DOS DADOS DA SONDA COM REGISTRADORES MODBUS ==========
        // Atualiza registradores com dados reais da MCT Task (MÉTODO ANTIGO - mantido)
        extern volatile int16_t sonda_heatValue_sync;
        extern volatile int16_t sonda_lambdaValue_sync;
        extern volatile int16_t sonda_heatRef_sync;
        extern volatile int16_t sonda_lambdaRef_sync;
        extern volatile uint16_t sonda_o2Percent_sync;
        extern volatile uint32_t sonda_output_sync;
        
        // ========== PROTEÇÃO MUTEX: Atualização de reg2000 e reg4000 ==========
        if (modbus_lock_registers(100)) {
            // Sincroniza reg4000 com dados atuais da sonda
            reg4000[lambdaValue] = sonda_lambdaValue_sync;
            reg4000[lambdaRef] = sonda_lambdaRef_sync;
            reg4000[heatValue] = sonda_heatValue_sync;
            reg4000[heatRef] = sonda_heatRef_sync;
            reg4000[output_mb] = (uint16_t)(sonda_output_sync & 0xFFFF); // Trunca para 16 bits
            
            // FALLBACK: Se não recebeu via fila, usa a variável global (compatibilidade)
            if (messages_processed == 0) {
                reg2000[dataValue] = sonda_o2Percent_sync; // Método antigo
                reg4000[PROBE_DEMAGED] = sonda_o2Percent_sync; // O2% nos registradores de diagnóstico
                ESP_LOGD(TAG, "📦 Usando fallback: O2=%d%% (variável global)", sonda_o2Percent_sync);
            }
            
            modbus_unlock_registers();
        } else {
            ESP_LOGW(TAG, "Timeout ao tentar lock para sincronizar registradores");
        }

        // DEBUG: Log de processamento Modbus
        static int modbus_cycle_count = 0;
        modbus_cycle_count++;
        if ((modbus_cycle_count % 1000) == 0) {
            ESP_LOGI(TAG, "Modbus: %d ciclos processados, msgs fila: %d", 
                     modbus_cycle_count, messages_processed);
        }

        (void)mbc_slave_check_event(MB_READ_WRITE_MASK);

        // // Obtém informações de eventos (com timeout seguro)
        ESP_ERROR_CHECK_WITHOUT_ABORT(mbc_slave_get_param_info(&reg_info, MB_PAR_INFO_GET_TOUT));

        if (reg_info.type & (MB_EVENT_HOLDING_REG_WR | MB_EVENT_HOLDING_REG_RD)) {
            ESP_LOGI(TAG, "HOLDING REG EVENT: ADDR=%u TYPE=%u", 
                     (unsigned)reg_info.mb_offset, (unsigned)reg_info.type);
            if (reg_info.type & MB_EVENT_HOLDING_REG_WR) {
                // ========== PROTEÇÃO MUTEX: Escrita de registrador detectada ==========
                // Nota: A escrita já foi feita pelo stack Modbus, mas precisamos
                // proteger o acesso ao save_config() que pode ler os registradores
                if (reg_info.mb_offset == 1000 + baudrate ||
                    reg_info.mb_offset == 1000 + endereco ||
                    reg_info.mb_offset == 1000 + paridade ||
                    reg_info.mb_offset == 2000 + dataValue) {
                    // Save config já lida internamente com acesso aos registradores
                    // Não precisa de mutex aqui pois o stack Modbus já protege a escrita
                    save_config();
                }
}

                     
                     
        }

        // DEBUG: Força processamento mais rápido para teste
        vTaskDelay(pdMS_TO_TICKS(5));  // 5ms em vez de 10ms 
    }

    // Nunca deve chegar aqui
    ESP_LOGI(TAG, "Modbus Slave Task terminated.");
    vTaskDelete(NULL);
}
