/*
 * MODBUS TCP SLAVE TASK IMPLEMENTATION
 * 
 * Implementação de Modbus TCP Slave que compartilha registradores com RTU.
 * Baseado no projeto TCP original, adaptado para o sistema modular atual.
 * 
 * Arquitetura:
 * - Compartilha reg1000~reg9000 com RTU via mutex
 * - Usa mbc_slave_init_tcp() para protocolo TCP/IP
 * - Configuração via WebServer (enabled/disabled)
 * - Prioridade menor que RTU (TCP=3, RTU=4)
 */

#include "modbus_tcp_slave_task.h"
#include "modbus_params.h"
#include "config_manager.h"

#include "esp_log.h"
#include "esp_modbus_slave.h"
#include "mbcontroller.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "wifi_manager.h"
#include "nvs_flash.h"
#include "nvs.h"

#include <string.h>

// ========== SISTEMA DE CONTROLE DE LOGS VIA FLAGS ==========
#include "main_config_flags.h"  // Para acessar FLAGS de configuração

static const char *TAG = "MODBUS_TCP";

// ================= FUNÇÕES DE INICIALIZAÇÃO E BACKUP NVS =================

/**
 * @brief Salva uma faixa de registradores no NVS
 * @param namespace Nome do namespace NVS (ex: "reg1000", "reg2000")
 * @param data Ponteiro para os dados
 * @param size Tamanho dos dados em bytes
 * @return ESP_OK se sucesso
 */
static esp_err_t save_registers_to_nvs(const char *namespace, const void *data, size_t size) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(namespace, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao abrir NVS %s: %s", namespace, esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(nvs_handle, "data", data, size);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Backup NVS %s salvo (%d bytes)", namespace, size);
        }
    }

    nvs_close(nvs_handle);
    return err;
}

/**
 * @brief Carrega uma faixa de registradores do NVS
 * @param namespace Nome do namespace NVS
 * @param data Ponteiro para buffer de destino
 * @param size Tamanho esperado dos dados
 * @return ESP_OK se sucesso
 */
static esp_err_t load_registers_from_nvs(const char *namespace, void *data, size_t size) {
    nvs_handle_t nvs_handle;
    esp_err_t err;
    size_t required_size = size;

    err = nvs_open(namespace, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_blob(nvs_handle, "data", data, &required_size);
    nvs_close(nvs_handle);

    if (err == ESP_OK && required_size == size) {
        ESP_LOGI(TAG, "Restaurado NVS %s (%d bytes)", namespace, size);
    }

    return err;
}

/**
 * @brief Salva os valores da faixa 1000 no NVS como backup
 * @return ESP_OK se sucesso
 */
static esp_err_t save_reg1000_to_nvs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open("modbus_backup", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao abrir NVS para backup reg1000: %s", esp_err_to_name(err));
        return err;
    }

    // Salva os 3 valores principais da faixa 1000
    err = nvs_set_u16(nvs_handle, "reg1000_baud", holding_reg1000_params.reg1000[baudrate]);
    if (err == ESP_OK) {
        err = nvs_set_u16(nvs_handle, "reg1000_addr", holding_reg1000_params.reg1000[endereco]);
    }
    if (err == ESP_OK) {
        err = nvs_set_u16(nvs_handle, "reg1000_par", holding_reg1000_params.reg1000[paridade]);
    }

    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Backup NVS reg1000: baud=%d, addr=%d, par=%d",
                     holding_reg1000_params.reg1000[baudrate],
                     holding_reg1000_params.reg1000[endereco],
                     holding_reg1000_params.reg1000[paridade]);
        }
    }

    nvs_close(nvs_handle);
    return err;
}

/**
 * @brief Carrega os valores da faixa 1000 do NVS
 * @return ESP_OK se sucesso, ESP_ERR_NVS_NOT_FOUND se não existe backup
 */
static esp_err_t load_reg1000_from_nvs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err;
    uint16_t baud = 0, addr = 0, par = 0;

    err = nvs_open("modbus_backup", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    // Tenta carregar os valores
    err = nvs_get_u16(nvs_handle, "reg1000_baud", &baud);
    if (err == ESP_OK) {
        err = nvs_get_u16(nvs_handle, "reg1000_addr", &addr);
    }
    if (err == ESP_OK) {
        err = nvs_get_u16(nvs_handle, "reg1000_par", &par);
    }

    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        // Valida se os valores NVS são válidos (não zerados)
        if (baud == 0 && addr == 0) {
            ESP_LOGW(TAG, "Valores NVS inválidos (zerados), descartando backup");
            return ESP_ERR_INVALID_STATE;
        }
        
        // Aplica valores carregados com proteção de mutex
        if (modbus_lock_registers(1000)) {
            holding_reg1000_params.reg1000[baudrate] = baud;
            holding_reg1000_params.reg1000[endereco] = addr;
            holding_reg1000_params.reg1000[paridade] = par;
            modbus_unlock_registers();
            
            ESP_LOGI(TAG, "Restaurado do NVS reg1000: baud=%d, addr=%d, par=%d",
                     baud, addr, par);
        } else {
            ESP_LOGW(TAG, "Timeout ao tentar lock para restaurar NVS");
            return ESP_ERR_TIMEOUT;
        }
    }

    return err;
}

/**
 * @brief Inicializa os valores da faixa 1000 com valores padrão
 */
static void init_reg1000_defaults(void) {
    ESP_LOGI(TAG, "Inicializando reg1000 com valores padrão...");
    
    if (modbus_lock_registers(1000)) {
        holding_reg1000_params.reg1000[baudrate] = 9600;
        holding_reg1000_params.reg1000[endereco] = 1;
        holding_reg1000_params.reg1000[paridade] = 0;
        modbus_unlock_registers();
        
        ESP_LOGI(TAG, "reg1000 inicializado: baud=9600, addr=1, par=0");
    } else {
        ESP_LOGW(TAG, "Timeout ao inicializar reg1000");
    }
}

/**
 * @brief Inicializa valores padrão de todas as faixas de registradores
 */
static void init_all_registers_defaults(void) {
    ESP_LOGI(TAG, "Inicializando todos os registradores com valores padrão...");
    
    if (!modbus_lock_registers(5000)) {
        ESP_LOGW(TAG, "Timeout ao inicializar registradores");
        return;
    }
    
    // Faixa 1000 (configuração RTU)
    holding_reg1000_params.reg1000[baudrate] = 9600;
    holding_reg1000_params.reg1000[endereco] = 1;
    holding_reg1000_params.reg1000[paridade] = 0;
    
    // Faixa 2000 (dados de monitoramento) - valores iniciais
    for (int i = 0; i < REG_DATA_SIZE; i++) {
        reg2000[i] = 2000 + i;
    }
    
    // Faixa 4000 (sonda Lambda)
    for (int i = 0; i < REG_4000_SIZE; i++) {
        reg4000[i] = 4000 + i;
    }
    
    // Faixa 6000 (DAC)
    for (int i = 0; i < REG_6000_SIZE; i++) {
        reg6000[i] = 6000 + i;
    }
    
    // Faixa 9000 (especificações)
    for (int i = 0; i < REG_UNITSPECS_SIZE; i++) {
        reg9000[i] = 9000 + (i * 10);
    }
    
    modbus_unlock_registers();
    ESP_LOGI(TAG, "Todos os registradores inicializados");
}

/**
 * @brief Salva todas as faixas de registradores no NVS
 */
static void save_all_registers_to_nvs(void) {
    if (!modbus_lock_registers(2000)) {
        ESP_LOGW(TAG, "Timeout ao salvar registradores no NVS");
        return;
    }
    
    save_registers_to_nvs("reg1000_bkp", holding_reg1000_params.reg1000, sizeof(holding_reg1000_params));
    save_registers_to_nvs("reg2000_bkp", reg2000, sizeof(reg2000));
    save_registers_to_nvs("reg4000_bkp", reg4000, sizeof(reg4000));
    save_registers_to_nvs("reg6000_bkp", reg6000, sizeof(reg6000));
    save_registers_to_nvs("reg9000_bkp", reg9000, sizeof(reg9000));
    
    modbus_unlock_registers();
    ESP_LOGI(TAG, "Todos os registradores salvos no NVS");
}

/**
 * @brief Carrega todas as faixas de registradores do NVS
 * @return ESP_OK se pelo menos uma faixa foi carregada com sucesso
 */
static esp_err_t load_all_registers_from_nvs(void) {
    int success_count = 0;
    
    if (!modbus_lock_registers(2000)) {
        ESP_LOGW(TAG, "Timeout ao carregar registradores do NVS");
        return ESP_ERR_TIMEOUT;
    }
    
    if (load_registers_from_nvs("reg1000_bkp", holding_reg1000_params.reg1000, sizeof(holding_reg1000_params)) == ESP_OK) {
        // Valida reg1000
        if (holding_reg1000_params.reg1000[baudrate] != 0 && holding_reg1000_params.reg1000[endereco] != 0) {
            success_count++;
        } else {
            ESP_LOGW(TAG, "reg1000 do NVS inválido (zerado)");
        }
    }
    
    if (load_registers_from_nvs("reg2000_bkp", reg2000, sizeof(reg2000)) == ESP_OK) success_count++;
    if (load_registers_from_nvs("reg4000_bkp", reg4000, sizeof(reg4000)) == ESP_OK) success_count++;
    if (load_registers_from_nvs("reg6000_bkp", reg6000, sizeof(reg6000)) == ESP_OK) success_count++;
    if (load_registers_from_nvs("reg9000_bkp", reg9000, sizeof(reg9000)) == ESP_OK) success_count++;
    
    modbus_unlock_registers();
    
    if (success_count > 0) {
        ESP_LOGI(TAG, "%d faixas restauradas do NVS", success_count);
        return ESP_OK;
    }
    
    return ESP_FAIL;
}

/**
 * @brief Carrega valores da faixa 1000 (prioridade: arquivo JSON > NVS > padrão)
 */
static void load_reg1000_values(void) {
    ESP_LOGI(TAG, "Carregando valores de todas as faixas de registradores...");
    
    // 1ª tentativa: Carregar do arquivo de configuração RTU
    esp_err_t err = load_config();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Valores carregados do arquivo rtu_config.json");
        // Salva no NVS como backup
        save_all_registers_to_nvs();
        return;
    }
    
    ESP_LOGW(TAG, "Falha ao carregar do arquivo: %s", esp_err_to_name(err));
    
    // 2ª tentativa: Carregar do NVS (backup)
    err = load_all_registers_from_nvs();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Valores restaurados do backup NVS");
        return;
    }
    
    ESP_LOGW(TAG, "Sem backup NVS disponível: %s", esp_err_to_name(err));
    
    // 3ª tentativa: Usar valores padrão
    ESP_LOGI(TAG, "Usando valores padrão");
    init_all_registers_defaults();
    
    // Salva os valores padrão no NVS para próxima vez
    save_all_registers_to_nvs();
}

// ================= VARIÁVEIS GLOBAIS =================
static TaskHandle_t tcp_task_handle = NULL;
static modbus_tcp_state_t tcp_state = MODBUS_TCP_STATE_STOPPED;
static void* mbc_tcp_slave_handler = NULL;

// ================= DEFINIÇÕES DE REGISTRADORES (IGUAL AO RTU) =================
#define HOLD_OFFSET(field) ((uint16_t)(offsetof(holding_reg_params_t, field) >> 1))
#define INPUT_OFFSET(field) ((uint16_t)(offsetof(input_reg_params_t, field) >> 1))

#define MB_REG_DISCRETE_INPUT_START         (0x0000)
#define MB_REG_COILS_START                  (0x0000)
#define MB_REG_INPUT_START_AREA0            (INPUT_OFFSET(input_data0))
#define MB_REG_INPUT_START_AREA1            (INPUT_OFFSET(input_data4))
#define MB_REG_HOLDING_START_AREA0          (HOLD_OFFSET(holding_data0))
#define MB_REG_HOLDING_START_AREA1          (HOLD_OFFSET(holding_data4))

#define MB_PAR_INFO_GET_TOUT                (10)
#define MB_READ_MASK                        (MB_EVENT_INPUT_REG_RD \
                                                | MB_EVENT_HOLDING_REG_RD \
                                                | MB_EVENT_DISCRETE_RD \
                                                | MB_EVENT_COILS_RD)
#define MB_WRITE_MASK                       (MB_EVENT_HOLDING_REG_WR \
                                                | MB_EVENT_COILS_WR)
#define MB_READ_WRITE_MASK                  (MB_READ_MASK | MB_WRITE_MASK)

// ================= FUNÇÕES AUXILIARES =================

/**
 * @brief Registra os descritores de área de registradores Modbus TCP
 * @note USA OS MESMOS ARRAYS DO RTU (compartilhados via mutex)
 */
static esp_err_t modbus_tcp_register_areas(void) {
    mb_register_area_descriptor_t reg_area;
    esp_err_t err;

    ESP_LOGI(TAG, "Registrando áreas de memória (compartilhadas com RTU)...");

    // ========== COILS ==========
    reg_area.type = MB_PARAM_COIL;
    reg_area.start_offset = MB_REG_COILS_START;
    reg_area.address = (void*)&coil_reg_params;
    reg_area.size = sizeof(coil_reg_params);
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao registrar COILS: %s", esp_err_to_name(err));
        return err;
    }

    // ========== DISCRETE INPUTS ==========
    reg_area.type = MB_PARAM_DISCRETE;
    reg_area.start_offset = MB_REG_DISCRETE_INPUT_START;
    reg_area.address = (void*)&discrete_reg_params;
    reg_area.size = sizeof(discrete_reg_params);
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao registrar DISCRETE: %s", esp_err_to_name(err));
        return err;
    }

    // ========== INPUT REGISTERS AREA 0 ==========
    reg_area.type = MB_PARAM_INPUT;
    reg_area.start_offset = MB_REG_INPUT_START_AREA0;
    reg_area.address = (void*)&input_reg_params.input_data0;
    reg_area.size = sizeof(float) << 2;
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao registrar INPUT AREA0: %s", esp_err_to_name(err));
        return err;
    }

    // ========== INPUT REGISTERS AREA 1 ==========
    reg_area.type = MB_PARAM_INPUT;
    reg_area.start_offset = MB_REG_INPUT_START_AREA1;
    reg_area.address = (void*)&input_reg_params.input_data4;
    reg_area.size = sizeof(float) << 2;
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao registrar INPUT AREA1: %s", esp_err_to_name(err));
        return err;
    }

    // ========== HOLDING REGISTERS AREA 0 ==========
    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = MB_REG_HOLDING_START_AREA0;
    reg_area.address = (void*)&holding_reg_params.holding_data0;
    reg_area.size = (MB_REG_HOLDING_START_AREA1 - MB_REG_HOLDING_START_AREA0) << 1;
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao registrar HOLDING AREA0: %s", esp_err_to_name(err));
        return err;
    }

    // ========== HOLDING REGISTERS AREA 1 ==========
    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = MB_REG_HOLDING_START_AREA1;
    reg_area.address = (void*)&holding_reg_params.holding_data4;
    reg_area.size = sizeof(float) << 2;
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao registrar HOLDING AREA1: %s", esp_err_to_name(err));
        return err;
    }

    // ========== FAIXA 1000 - Configuração RTU ==========
    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = 1000;
    reg_area.address = (void*)&holding_reg1000_params.reg1000;
    reg_area.size = sizeof(holding_reg1000_params);
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao registrar REG1000: %s", esp_err_to_name(err));
        return err;
    }

    // ========== FAIXA 2000 - Dados ==========
    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = REG_DATA_START;
    reg_area.address = (void*)&reg2000;
    reg_area.size = sizeof(reg2000);
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao registrar REG2000: %s", esp_err_to_name(err));
        return err;
    }

    // ========== FAIXA 3000 - DAC Min/Max ==========
    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = REG_3000_START;
    reg_area.address = (void*)&reg3000;
    reg_area.size = sizeof(reg3000);
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao registrar REG3000: %s", esp_err_to_name(err));
        return err;
    }

    // ========== FAIXA 4000 - Lambda/Heat ==========
    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = REG_4000_START;
    reg_area.address = (void*)&reg4000;
    reg_area.size = sizeof(reg4000);
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao registrar REG4000: %s", esp_err_to_name(err));
        return err;
    }

    // ========== FAIXA 5000 - Testes ==========
    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = REG_5000_START;
    reg_area.address = (void*)&reg5000;
    reg_area.size = sizeof(reg5000);
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao registrar REG5000: %s", esp_err_to_name(err));
        return err;
    }

    // ========== FAIXA 6000 - DAC ==========
    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = REG_6000_START;
    reg_area.address = (void*)&reg6000;
    reg_area.size = sizeof(reg6000);
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao registrar REG6000: %s", esp_err_to_name(err));
        return err;
    }

    // ========== FAIXA 7000 ==========
    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = REG_7000_START;
    reg_area.address = (void*)&reg7000;
    reg_area.size = sizeof(reg7000);
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao registrar REG7000: %s", esp_err_to_name(err));
        return err;
    }

    // ========== FAIXA 8000 ==========
    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = REG_8000_START;
    reg_area.address = (void*)&reg8000;
    reg_area.size = sizeof(reg8000);
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao registrar REG8000: %s", esp_err_to_name(err));
        return err;
    }

    // ========== FAIXA 9000 - Especificações ==========
    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = REG_UNITSPECS_START;
    reg_area.address = (void*)&reg9000;
    reg_area.size = sizeof(reg9000);
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao registrar REG9000: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Todas as áreas de memória registradas com sucesso!");
    return ESP_OK;
}

// ================= TASK PRINCIPAL =================

void modbus_tcp_slave_task(void *pvParameters) {
    mb_param_info_t reg_info;
    mb_communication_info_t comm_info = {0};
    
    ESP_LOGI(TAG, "Modbus TCP Slave Task iniciando...");
    tcp_state = MODBUS_TCP_STATE_INITIALIZING;

    // ========== INICIALIZA MUTEX (SE AINDA NÃO ESTIVER) ==========
    if (!modbus_mutex_init()) {
        ESP_LOGW(TAG, "Mutex já inicializado ou falha ao criar");
    } else {
        ESP_LOGI(TAG, "Mutex de registradores inicializado pelo TCP");
    }

    // ========== CARREGA VALORES DOS REGISTRADORES ==========
    load_reg1000_values();

    // ========== VERIFICA SE ESTÁ HABILITADO ==========
    modbus_tcp_config_t tcp_config;
    esp_err_t config_result = load_modbus_tcp_config(&tcp_config);
    
    if (config_result != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao carregar configuração TCP: %s", esp_err_to_name(config_result));
        tcp_state = MODBUS_TCP_STATE_STOPPED;
        vTaskDelete(NULL);
        return;
    }
    
    // CRIAÇÃO AUTOMÁTICA DO ARQUIVO SE NÃO EXISTIR
    if (!tcp_config.enabled) {
        ESP_LOGW(TAG, "TCP desabilitado, criando arquivo de configuração padrão com enabled=true...");
        
        // Cria arquivo diretamente
        FILE *config_file = fopen("/spiffs/data/config/modbus_tcp_config.json", "w");
        if (config_file) {
            fprintf(config_file, "{\"enabled\":true,\"port\":502,\"max_connections\":5}");
            fclose(config_file);
            ESP_LOGI(TAG, "Arquivo de configuração TCP criado!");
            
            // Recarrega configuração
            config_result = load_modbus_tcp_config(&tcp_config);
            if (config_result != ESP_OK) {
                ESP_LOGW(TAG, "Falha ao recarregar, forçando enabled=true");
                tcp_config.enabled = true;
            }
        } else {
            ESP_LOGW(TAG, "Não foi possível criar arquivo, forçando enabled=true");
            tcp_config.enabled = true;
        }
    }
    
    if (!tcp_config.enabled) {
        ESP_LOGW(TAG, "Modbus TCP ainda desabilitado na configuração");
        tcp_state = MODBUS_TCP_STATE_STOPPED;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "TCP Config: porta=%d, max_conn=%d", 
             tcp_config.port, tcp_config.max_connections);
             
    // DEBUG: Vamos verificar se o arquivo existe fisicamente
    FILE *test_file = fopen("/spiffs/data/config/modbus_tcp_config.json", "r");
    if (test_file) {
        char buffer[256];
        size_t bytes_read = fread(buffer, 1, sizeof(buffer) - 1, test_file);
        buffer[bytes_read] = '\0';
        fclose(test_file);
        ESP_LOGI(TAG, "DEBUG: Arquivo TCP existe! Conteúdo: %s", buffer);
    } else {
        ESP_LOGW(TAG, "DEBUG: Arquivo TCP não existe em /spiffs/data/config/");
        
        // Testa caminho antigo
        test_file = fopen("/spiffs/modbus_tcp_config.json", "r");
        if (test_file) {
            ESP_LOGI(TAG, "DEBUG: Arquivo TCP encontrado no caminho antigo!");
            fclose(test_file);
        } else {
            ESP_LOGW(TAG, "DEBUG: Arquivo TCP não encontrado em lugar nenhum!");
        }
    }

    // ========== VERIFICA WIFI (AP OU STA) ==========
    wifi_status_t wifi_st = wifi_get_status();
    bool wifi_ready = false;
    
    if (wifi_st.is_connected && wifi_st.ip_address[0] != '\0') {
        // WiFi STA conectado
        ESP_LOGI(TAG, "WiFi STA conectado: IP=%s", wifi_st.ip_address);
        wifi_ready = true;
    } else if (wifi_st.ap_active) {
        // WiFi AP ativo - TCP pode funcionar localmente
        ESP_LOGI(TAG, "WiFi AP ativo - TCP funcionará localmente");
        wifi_ready = true;
    }
    
    if (!wifi_ready) {
        ESP_LOGW(TAG, "WiFi não disponível! TCP precisa de AP ou STA ativo.");
        ESP_LOGW(TAG, "Status WiFi: AP=%s, STA=%s, IP=%s", 
                wifi_st.ap_active ? "ON" : "OFF",
                wifi_st.is_connected ? "ON" : "OFF", 
                wifi_st.ip_address);
        tcp_state = MODBUS_TCP_STATE_ERROR;
        vTaskDelay(pdMS_TO_TICKS(10000)); // Aguarda 10s antes de tentar novamente
        ESP_LOGW(TAG, "Tentando novamente...");
        esp_restart(); // Reinicia para tentar reconexão
        return;
    }

    // ========== INICIALIZA MODBUS TCP ==========
    esp_err_t err = mbc_slave_init_tcp(&mbc_tcp_slave_handler);
    if (err != ESP_OK || mbc_tcp_slave_handler == NULL) {
        ESP_LOGE(TAG, "FATAL: Falha ao inicializar Modbus TCP: %s", 
                 esp_err_to_name(err));
        tcp_state = MODBUS_TCP_STATE_ERROR;
        vTaskDelete(NULL);
        return;
    }

    // ========== CONFIGURAÇÃO TCP ==========
    comm_info.ip_addr_type = MB_IPV4;           // IPv4 apenas
    comm_info.ip_mode = MB_MODE_TCP;            // Modo TCP
    comm_info.ip_port = tcp_config.port;        // Porta (padrão 502)
    comm_info.ip_addr = NULL;                   // Bind em todas interfaces
    
    // Tenta obter interface WiFi (STA primeiro, AP depois)
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) {
        netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        ESP_LOGW(TAG, "STA não disponível, usando AP para Modbus TCP");
    } else {
        ESP_LOGI(TAG, "Usando interface STA para Modbus TCP");
    }
    
    comm_info.ip_netif_ptr = (void*)netif;
    comm_info.slave_uid = CONFIG_MB_SLAVE_ADDR; // Mesmo slave ID do RTU

    err = mbc_slave_setup((void*)&comm_info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao configurar Modbus TCP: %s", esp_err_to_name(err));
        tcp_state = MODBUS_TCP_STATE_ERROR;
        mbc_slave_destroy();
        vTaskDelete(NULL);
        return;
    }

    // ========== REGISTRA ÁREAS DE MEMÓRIA ==========
    err = modbus_tcp_register_areas();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao registrar áreas de memória");
        tcp_state = MODBUS_TCP_STATE_ERROR;
        mbc_slave_destroy();
        vTaskDelete(NULL);
        return;
    }

    // ========== INICIA STACK MODBUS TCP ==========
    err = mbc_slave_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar stack Modbus TCP: %s", esp_err_to_name(err));
        tcp_state = MODBUS_TCP_STATE_ERROR;
        mbc_slave_destroy();
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Modbus TCP Slave iniciado! Porta: %d", tcp_config.port);
    ESP_LOGI(TAG, "Aguardando conexões TCP...");
    tcp_state = MODBUS_TCP_STATE_RUNNING;

    // ========== LOOP PRINCIPAL ==========
    for (;;) {
        // Verifica eventos Modbus (leitura/escrita)
        (void)mbc_slave_check_event(MB_READ_WRITE_MASK);

        // Obtém informações do evento
        err = mbc_slave_get_param_info(&reg_info, MB_PAR_INFO_GET_TOUT);
        if (err == ESP_OK) {
            const char* rw_str = (reg_info.type & MB_READ_MASK) ? "READ" : "WRITE";

            // ========== EVENTOS DE HOLDING REGISTERS ==========
            if (reg_info.type & (MB_EVENT_HOLDING_REG_WR | MB_EVENT_HOLDING_REG_RD)) {
                if (FLAGS.log_modbus_tcp) {
                    ESP_LOGI(TAG, "HOLDING %s: ADDR=%u, SIZE=%u", 
                             rw_str, (unsigned)reg_info.mb_offset, (unsigned)reg_info.size);
                }

                // Se foi escrita em registradores críticos, salva config
                if (reg_info.type & MB_EVENT_HOLDING_REG_WR) {
                    if (reg_info.mb_offset >= 1000 && reg_info.mb_offset < 10000) {
                        ESP_LOGI(TAG, "Salvando configuração alterada via TCP");
                        
                        // Salva arquivo JSON (se faixa 1000)
                        if (reg_info.mb_offset >= 1000 && reg_info.mb_offset < 2000) {
                            save_config();
                        }
                        
                        // Salva backup NVS de todas as faixas modificadas
                        save_all_registers_to_nvs();
                    }
                }
            }

            // ========== EVENTOS DE INPUT REGISTERS ==========
            else if (reg_info.type & MB_EVENT_INPUT_REG_RD) {
                ESP_LOGD(TAG, "📖 INPUT READ: ADDR=%u, SIZE=%u", 
                         (unsigned)reg_info.mb_offset, (unsigned)reg_info.size);
            }

            // ========== EVENTOS DE COILS ==========
            else if (reg_info.type & (MB_EVENT_COILS_RD | MB_EVENT_COILS_WR)) {
                ESP_LOGI(TAG, "🔘 COILS %s: ADDR=%u, SIZE=%u", 
                         rw_str, (unsigned)reg_info.mb_offset, (unsigned)reg_info.size);
            }

            // ========== EVENTOS DE DISCRETE INPUTS ==========
            else if (reg_info.type & MB_EVENT_DISCRETE_RD) {
                ESP_LOGD(TAG, "📖 DISCRETE READ: ADDR=%u, SIZE=%u", 
                         (unsigned)reg_info.mb_offset, (unsigned)reg_info.size);
            }
        }

        // Delay pequeno para não sobrecarregar CPU
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Nunca deve chegar aqui
    ESP_LOGI(TAG, "🛑 Modbus TCP Slave Task terminando...");
    tcp_state = MODBUS_TCP_STATE_STOPPED;
    vTaskDelete(NULL);
}

// ================= FUNÇÕES PÚBLICAS =================

esp_err_t modbus_tcp_slave_init(void) {
    ESP_LOGI(TAG, "🏗️ modbus_tcp_slave_init() chamada");
    
    // Verifica se já está rodando
    if (tcp_task_handle != NULL) {
        ESP_LOGW(TAG, "Task TCP já existe");
        return ESP_ERR_INVALID_STATE;
    }

    // Verifica se está habilitado
    if (!modbus_tcp_is_enabled()) {
        ESP_LOGW(TAG, "ℹ️ Modbus TCP desabilitado na configuração, não criando task");
        return ESP_OK; // Não é erro, apenas não cria
    }

    ESP_LOGI(TAG, "Modbus TCP habilitado na configuração! Criando task...");

    // Cria a task
    BaseType_t result = xTaskCreatePinnedToCore(
        modbus_tcp_slave_task,
        "modbus_tcp",
        MODBUS_TCP_TASK_STACK_SIZE,
        NULL,
        MODBUS_TCP_TASK_PRIORITY,
        &tcp_task_handle,
        MODBUS_TCP_TASK_CORE
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Falha ao criar task TCP");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Task Modbus TCP criada (prioridade=%d, core=%d, handle=%p)", 
             MODBUS_TCP_TASK_PRIORITY, MODBUS_TCP_TASK_CORE, tcp_task_handle);
    return ESP_OK;
}

esp_err_t modbus_tcp_start(void) {
    if (tcp_task_handle != NULL) {
        ESP_LOGW(TAG, "TCP já está rodando");
        return ESP_OK;
    }
    return modbus_tcp_slave_init();
}

esp_err_t modbus_tcp_stop(void) {
    if (tcp_task_handle == NULL) {
        ESP_LOGW(TAG, "TCP não está rodando");
        return ESP_OK;
    }

    tcp_state = MODBUS_TCP_STATE_STOPPING;
    
    // Destrói o stack Modbus
    if (mbc_tcp_slave_handler != NULL) {
        mbc_slave_destroy();
        mbc_tcp_slave_handler = NULL;
    }

    // Deleta a task
    if (tcp_task_handle != NULL) {
        vTaskDelete(tcp_task_handle);
        tcp_task_handle = NULL;
    }

    tcp_state = MODBUS_TCP_STATE_STOPPED;
    ESP_LOGI(TAG, "🛑 Modbus TCP parado");
    return ESP_OK;
}

modbus_tcp_state_t modbus_tcp_get_state(void) {
    return tcp_state;
}

bool modbus_tcp_is_enabled(void) {
    modbus_tcp_config_t config;
    if (load_modbus_tcp_config(&config) != ESP_OK) {
        return false;
    }
    return config.enabled;
}

TaskHandle_t modbus_tcp_get_task_handle(void) {
    return tcp_task_handle;
}

esp_err_t modbus_tcp_slave_destroy(void) {
    return modbus_tcp_stop();
}
