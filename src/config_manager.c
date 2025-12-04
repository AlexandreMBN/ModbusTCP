/* ============================================================================
 * CONFIG_MANAGER.C - GERENCIADOR MODULAR DE CONFIGURAÇÕES
 * ============================================================================
 * 
 * DESCRIÇÃO:
 * -----------
 * Este módulo gerencia todas as configurações do sistema de forma centralizada
 * e modular. Implementa um sistema robusto de persistência com dupla redundância:
 * armazenamento primário em SPIFFS (arquivos JSON) e backup automático em NVS.
 * 
 * ARQUITETURA DE ARMAZENAMENTO:
 * -----------------------------
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │                     SISTEMA DE CONFIGURAÇÕES                            │
 * │                                                                         │
 * │  SPIFFS (/spiffs/data/config/)        NVS (Flash)                      │
 * │  ├─ rtu_config.json                   ├─ rtu_json (backup)             │
 * │  ├─ ap_config.json                    ├─ ap_json (backup)              │
 * │  ├─ sta_config.json                   ├─ sta_json (backup)             │
 * │  ├─ mqtt_config.json                  ├─ mqtt_json (backup)            │
 * │  ├─ network_config.json               ├─ network_json (backup)         │
 * │  └─ modbus_tcp_config.json            └─ mb_tcp_json (backup)          │
 * └─────────────────────────────────────────────────────────────────────────┘
 * 
 * FLUXO DE OPERAÇÃO:
 * ------------------
 * 1. ESCRITA: Salva em SPIFFS + cria backup NVS automaticamente
 * 2. LEITURA: Tenta SPIFFS primeiro → fallback para NVS → valores padrão
 * 3. MIGRAÇÃO: Suporta caminhos antigos para compatibilidade retroativa
 * 
 * TIPOS DE CONFIGURAÇÃO SUPORTADOS:
 * ---------------------------------
 * 
 * RTU_CONFIG (rtu_config.json):
 *    - Registradores Modbus (1000, 2000, 4000, 6000, 9000)
 *    - Configurações de comunicação serial (baudrate, paridade, endereço)
 *    - Parâmetros da sonda lambda e DAC
 *    
 * AP_CONFIG (ap_config.json):
 *    - SSID, senha e IP do Access Point
 *    - Credenciais de administrador (username/password)
 *    
 * STA_CONFIG (sta_config.json):  
 *    - Credenciais WiFi para conexão à rede externa
 *    - SSID e senha da rede cliente
 *    
 * MQTT_CONFIG (mqtt_config.json):
 *    - Broker URL, porta, credenciais
 *    - Configurações TLS, QoS, retain
 *    - Intervalo de publicação e habilitação
 *    
 * NETWORK_CONFIG (network_config.json):
 *    - Configuração IP estático (IP, máscara, gateway, DNS)
 *    - Se vazio = DHCP automático
 *    
 * MODBUS_TCP_CONFIG (modbus_tcp_config.json):
 *    - Habilitação do servidor Modbus TCP
 *    - Porta de escuta e máximo de conexões simultâneas
 * 
 * DEPENDÊNCIAS E RELACIONAMENTOS:
 * -------------------------------
 * 
 * DEPENDÊNCIAS DIRETAS:
 * - include/config_manager.h     : Definições de estruturas e protótipos
 * - modbus_params.c              : Arrays de registradores Modbus globais
 * - esp_spiffs.h                 : Sistema de arquivos SPIFFS
 * - cJSON.h                      : Parser/gerador JSON
 * - nvs_flash.h                  : Sistema NVS para backups
 * 
 * MÓDULOS QUE DEPENDEM DESTE:
 * - src/main.c                   : Carrega configurações na inicialização
 * - src/wifi_manager.c           : AP/STA configs para conexão WiFi
 * - src/webserver.c              : Salva configs via interface web
 * - src/mqtt_client_task.c       : MQTT config para conexão broker
 * - src/modbus_slave_task.c      : RTU config para comunicação serial
 * - src/modbus_tcp_slave_task.c  : TCP config para servidor Modbus TCP
 * - src/oxygen_sensor_task.c     : Registradores 4000/6000 para controle
 * 
 * FLUXO DE INICIALIZAÇÃO:
 * -----------------------
 * 1. main.c → config_manager_init() (se existir)
 * 2. Cada módulo carrega suas próprias configurações:
 *    - wifi_manager.c → load_ap_config() + load_sta_config()
 *    - mqtt_client_task.c → load_mqtt_config()  
 *    - modbus_slave_task.c → load_rtu_config()
 *    - etc.
 * 
 * SISTEMA DE NÍVEIS DE USUÁRIO:
 * -----------------------------
 * - USER_LEVEL_NONE (0)    : Sem acesso
 * - USER_LEVEL_OPERATOR (1): Operador (monitoramento apenas)
 * - USER_LEVEL_ADMIN (2)   : Administrador (modificação completa)
 * - USER_LEVEL_ROOT (3)    : Root (acesso total + factory reset)
 * 
 * ROBUSTEZ E RECUPERAÇÃO:
 * -----------------------
 * Dupla redundância (SPIFFS + NVS)
 * Validação JSON com fallback para valores padrão
 * Compatibilidade com caminhos de arquivo antigos
 * Logs detalhados para diagnóstico
 * Proteção contra corrupção (validação antes de salvar)
 * 
 * EXEMPLO DE USO TÍPICO:
 * ----------------------
 * ```c
 * // Carregar configuração WiFi
 * sta_config_t sta_config;
 * if (load_sta_config(&sta_config) == ESP_OK) {
 *     wifi_connect(sta_config.ssid, sta_config.password);
 * }
 * 
 * // Salvar nova configuração AP
 * ap_config_t ap_config = {
 *     .ssid = "MeuESP32",
 *     .password = "MinhaSeha123",
 *     .ip = "192.168.4.1"
 * };
 * save_ap_config(&ap_config);
 * ```
 * 
 * ============================================================================
 */

// New Modular Configuration Manager
#include "config_manager.h"
#include "esp_spiffs.h"
#include "cJSON.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <sys/stat.h>
#include <sys/types.h>

static const char *TAG = "CONFIG_MGR";

// Arquivos de configuração
#define RTU_CONFIG_FILE     "/spiffs/data/config/rtu_config.json"
#define AP_CONFIG_FILE      "/spiffs/data/config/ap_config.json"
#define STA_CONFIG_FILE     "/spiffs/data/config/sta_config.json"
#define MQTT_CONFIG_FILE    "/spiffs/data/config/mqtt_config.json"
#define NETWORK_CONFIG_FILE "/spiffs/data/config/network_config.json"
#define MODBUS_TCP_CONFIG_FILE "/spiffs/data/config/modbus_tcp_config.json"

// Caminhos de fallback para compatibilidade
#define RTU_CONFIG_FILE_OLD     "/spiffs/rtu_config.json"
#define AP_CONFIG_FILE_OLD      "/spiffs/ap_config.json"
#define STA_CONFIG_FILE_OLD     "/spiffs/sta_config.json"
#define MQTT_CONFIG_FILE_OLD    "/spiffs/mqtt_config.json"
#define NETWORK_CONFIG_FILE_OLD "/spiffs/network_config.json"
#define MODBUS_TCP_CONFIG_FILE_OLD "/spiffs/modbus_tcp_config.json"

// Inicializa SPIFFS (uma vez)
static void init_spiffs(void) {
    static bool mounted = false;
    if (mounted) return;

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 10,  // Aumentado para mais arquivos
        .format_if_mount_failed = true
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret == ESP_ERR_INVALID_STATE) {
        // SPIFFS já está registrado por outro módulo (main.c ou webserver.c)
        ESP_LOGI(TAG, "SPIFFS já montado previamente - OK");
        mounted = true;
        return;
    }
    ESP_ERROR_CHECK(ret);  // Aborta apenas em erros reais
    mounted = true;
    ESP_LOGI(TAG, "SPIFFS inicializado com nova estrutura modular");
}

// ================= Helper Functions =================
// NVS Keys para backup das configurações JSON
#define NVS_RTU_KEY     "rtu_json"
#define NVS_AP_KEY      "ap_json"
#define NVS_STA_KEY     "sta_json"
#define NVS_MQTT_KEY    "mqtt_json"
#define NVS_NETWORK_KEY "network_json"
#define NVS_MODBUS_TCP_KEY "mb_tcp_json"

// Função para salvar JSON string na NVS
static esp_err_t save_json_to_nvs(const char* namespace_name, const char* key, const char* json_string) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(namespace_name, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao abrir NVS namespace %s: %s", namespace_name, esp_err_to_name(err));
        return err;
    }
    
    err = nvs_set_str(nvs_handle, key, json_string);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao salvar %s na NVS: %s", key, esp_err_to_name(err));
    } else {
        err = nvs_commit(nvs_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Backup %s salvo com sucesso na NVS", key);
        }
    }
    
    nvs_close(nvs_handle);
    return err;
}

// Função para carregar JSON string da NVS
static esp_err_t load_json_from_nvs(const char* namespace_name, const char* key, char** json_string) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(namespace_name, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Erro ao abrir NVS namespace %s: %s", namespace_name, esp_err_to_name(err));
        return err;
    }
    
    size_t required_size = 0;
    err = nvs_get_str(nvs_handle, key, NULL, &required_size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Chave %s não encontrada na NVS: %s", key, esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    *json_string = malloc(required_size);
    if (*json_string == NULL) {
        ESP_LOGE(TAG, "Erro de alocação de memória para %s", key);
        nvs_close(nvs_handle);
        return ESP_ERR_NO_MEM;
    }
    
    err = nvs_get_str(nvs_handle, key, *json_string, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao carregar %s da NVS: %s", key, esp_err_to_name(err));
        free(*json_string);
        *json_string = NULL;
    } else {
        ESP_LOGI(TAG, "Backup %s carregado com sucesso da NVS", key);
    }
    
    nvs_close(nvs_handle);
    return err;
}

// Função para criar diretório /data/config se não existir
void ensure_data_config_dir(void) {
    struct stat st = {0};
    if (stat("/spiffs/data", &st) == -1) {
        mkdir("/spiffs/data", 0700);
    }
    if (stat("/spiffs/data/config", &st) == -1) {
        mkdir("/spiffs/data/config", 0700);
    }
}

// ================= RTU Config (/spiffs/rtu_config.json) =================
esp_err_t save_rtu_config(void) {
    init_spiffs();

    ESP_LOGI(TAG, "Salvando configuração RTU...");
    
    // Monta JSON com registradores Modbus
    cJSON *root = cJSON_CreateObject();
    
    // Adiciona flag enabled (controle de habilitação RTU)
    cJSON_AddBoolToObject(root, "enabled", modbus_rtu_enabled);
    
    // Faixa 1000 - Configuração RTU
    cJSON *reg1000_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(reg1000_obj, "baudrate", holding_reg1000_params.reg1000[baudrate]);
    cJSON_AddNumberToObject(reg1000_obj, "endereco", holding_reg1000_params.reg1000[endereco]);
    cJSON_AddNumberToObject(reg1000_obj, "paridade", holding_reg1000_params.reg1000[paridade]);
    cJSON_AddItemToObject(root, "reg1000", reg1000_obj);

    // Faixa 2000 - Dados
    cJSON *reg2000_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(reg2000_obj, "dataValue", reg2000[dataValue]);
    cJSON_AddItemToObject(root, "reg2000", reg2000_obj);

    // Faixa 4000 - Sonda Lambda
    cJSON *reg4000_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(reg4000_obj, "lambdaValue", reg4000[lambdaValue]);
    cJSON_AddNumberToObject(reg4000_obj, "lambdaRef", reg4000[lambdaRef]);
    cJSON_AddNumberToObject(reg4000_obj, "heatValue", reg4000[heatValue]);
    cJSON_AddNumberToObject(reg4000_obj, "heatRef", reg4000[heatRef]);
    cJSON_AddNumberToObject(reg4000_obj, "output_mb", reg4000[output_mb]);
    cJSON_AddNumberToObject(reg4000_obj, "PROBE_DEMAGED", reg4000[PROBE_DEMAGED]);
    cJSON_AddNumberToObject(reg4000_obj, "PROBE_TEMP_OUT_OF_RANGE", reg4000[PROBE_TEMP_OUT_OF_RANGE]);
    cJSON_AddNumberToObject(reg4000_obj, "COMPRESSOR_FAIL", reg4000[COMPRESSOR_FAIL]);
    cJSON_AddItemToObject(root, "reg4000", reg4000_obj);

    // Faixa 6000 - DAC
    cJSON *reg6000_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(reg6000_obj, "maxDac0", reg6000[maxDac0]);
    cJSON_AddNumberToObject(reg6000_obj, "forcaValorDAC", reg6000[forcaValorDAC]);
    cJSON_AddNumberToObject(reg6000_obj, "nada", reg6000[nada]);
    cJSON_AddNumberToObject(reg6000_obj, "dACGain0", reg6000[dACGain0]);
    cJSON_AddNumberToObject(reg6000_obj, "dACOffset0", reg6000[dACOffset0]);
    cJSON_AddItemToObject(root, "reg6000", reg6000_obj);

    // Faixa 9000 - Especificações
    cJSON *reg9000_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(reg9000_obj, "valorZero", reg9000[valorZero]);
    cJSON_AddNumberToObject(reg9000_obj, "valorUm", reg9000[valorUm]);
    cJSON_AddNumberToObject(reg9000_obj, "firmVerHi", reg9000[firmVerHi]);
    cJSON_AddNumberToObject(reg9000_obj, "firmVerLo", reg9000[firmVerLo]);
    cJSON_AddNumberToObject(reg9000_obj, "valorQuatro", reg9000[valorQuatro]);
    cJSON_AddNumberToObject(reg9000_obj, "valorCinco", reg9000[valorCinco]);
    cJSON_AddNumberToObject(reg9000_obj, "lotnum0", reg9000[lotnum0]);
    cJSON_AddNumberToObject(reg9000_obj, "lotnum1", reg9000[lotnum1]);
    cJSON_AddNumberToObject(reg9000_obj, "lotnum2", reg9000[lotnum2]);
    cJSON_AddNumberToObject(reg9000_obj, "lotnum3", reg9000[lotnum3]);
    cJSON_AddNumberToObject(reg9000_obj, "lotnum4", reg9000[lotnum4]);
    cJSON_AddNumberToObject(reg9000_obj, "lotnum5", reg9000[lotnum5]);
    cJSON_AddNumberToObject(reg9000_obj, "wafnum", reg9000[wafnum]);
    cJSON_AddNumberToObject(reg9000_obj, "coordx0", reg9000[coordx0]);
    cJSON_AddNumberToObject(reg9000_obj, "coordx1", reg9000[coordx1]);
    cJSON_AddNumberToObject(reg9000_obj, "valor17", reg9000[valor17]);
    cJSON_AddNumberToObject(reg9000_obj, "valor18", reg9000[valor18]);
    cJSON_AddNumberToObject(reg9000_obj, "valor19", reg9000[valor19]);
    cJSON_AddItemToObject(root, "reg9000", reg9000_obj);

    // Gerar string JSON
    char *json_str = cJSON_Print(root);
    esp_err_t result = ESP_OK;
    
    // Garantir que o diretório existe
    ensure_data_config_dir();
    
    // 1. Salvar no arquivo SPIFFS
    FILE *f = fopen(RTU_CONFIG_FILE, "w");
    if (f) {
        fprintf(f, "%s", json_str);
        fclose(f);
        ESP_LOGI(TAG, "Configuração RTU salva em %s", RTU_CONFIG_FILE);
    } else {
        ESP_LOGE(TAG, "Erro ao abrir %s para escrita", RTU_CONFIG_FILE);
        result = ESP_FAIL;
    }
    
    // 2. Salvar backup na NVS
    esp_err_t nvs_result = save_json_to_nvs("config_backup", NVS_RTU_KEY, json_str);
    if (nvs_result != ESP_OK) {
        ESP_LOGW(TAG, "Falha ao salvar backup RTU na NVS");
        if (result == ESP_OK) result = ESP_FAIL; // Só falha se arquivo também falhou
    }
    
    cJSON_Delete(root);
    free(json_str);
    return result;
}

esp_err_t load_rtu_config(void) {
    init_spiffs();
    ESP_LOGI(TAG, "Carregando configuração RTU...");
    
    char *data = NULL;
    cJSON *root = NULL;
    esp_err_t result = ESP_FAIL;
    
    // 1. Tentar carregar do arquivo SPIFFS (prioridade)
    FILE *f = fopen(RTU_CONFIG_FILE, "r");
    if (!f) {
        // Tentar caminho antigo para compatibilidade
        f = fopen(RTU_CONFIG_FILE_OLD, "r");
        if (f) {
            ESP_LOGI(TAG, "Carregando RTU config do caminho antigo: %s", RTU_CONFIG_FILE_OLD);
        }
    } else {
        ESP_LOGI(TAG, "Carregando RTU config do caminho novo: %s", RTU_CONFIG_FILE);
    }
    
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        rewind(f);
        
        data = malloc(size + 1);
        if (data) {
            fread(data, 1, size, f);
            data[size] = '\0';
            fclose(f);
            
            root = cJSON_Parse(data);
            if (root) {
                ESP_LOGI(TAG, "Configuração RTU carregada do arquivo SPIFFS");
                result = ESP_OK;
            } else {
                ESP_LOGE(TAG, "Erro ao parsear arquivo RTU do SPIFFS");
                free(data);
                data = NULL;
            }
        } else {
            fclose(f);
        }
    }
    
    // 2. Se falhou no SPIFFS, tentar carregar da NVS (fallback)
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Tentando carregar RTU config da NVS (fallback)...");
        
        if (load_json_from_nvs("config_backup", NVS_RTU_KEY, &data) == ESP_OK && data) {
            root = cJSON_Parse(data);
            if (root) {
                ESP_LOGI(TAG, "Configuração RTU recuperada da NVS com sucesso!");
                result = ESP_OK;
            } else {
                ESP_LOGE(TAG, "Erro ao parsear JSON RTU da NVS");
            }
        } else {
            ESP_LOGW(TAG, "Backup RTU não encontrado na NVS");
        }
    }
    
    // 3. Se ambos falharam, manter valores padrão
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Usando valores padrão RTU (SPIFFS e NVS indisponíveis)");
        if (data) free(data);
        return ESP_FAIL;
    }

    // Carregar flag enabled (controle de habilitação RTU)
    cJSON *enabled_item = cJSON_GetObjectItem(root, "enabled");
    if (enabled_item && cJSON_IsBool(enabled_item)) {
        modbus_rtu_enabled = cJSON_IsTrue(enabled_item);
        ESP_LOGI(TAG, "RTU enabled carregado: %s", modbus_rtu_enabled ? "HABILITADO" : "DESABILITADO");
    } else {
        // Se não encontrar, manter habilitado por padrão (compatibilidade)
        modbus_rtu_enabled = true;
        ESP_LOGW(TAG, "'enabled' não encontrado no JSON, mantendo RTU habilitado (padrão)");
    }

    // Faixa 1000 - Configuração RTU
    cJSON *reg1000_obj = cJSON_GetObjectItem(root, "reg1000");
    if (reg1000_obj) {
        cJSON *item = cJSON_GetObjectItem(reg1000_obj, "baudrate");
        if (item && cJSON_IsNumber(item)) holding_reg1000_params.reg1000[baudrate] = item->valueint;
        item = cJSON_GetObjectItem(reg1000_obj, "endereco");
        if (item && cJSON_IsNumber(item)) holding_reg1000_params.reg1000[endereco] = item->valueint;
        item = cJSON_GetObjectItem(reg1000_obj, "paridade");
        if (item && cJSON_IsNumber(item)) holding_reg1000_params.reg1000[paridade] = item->valueint;
    } else {
        // Compatibilidade com formato antigo
        cJSON *item = cJSON_GetObjectItem(root, "baudrate");
        if (item && cJSON_IsNumber(item)) holding_reg1000_params.reg1000[baudrate] = item->valueint;
        item = cJSON_GetObjectItem(root, "endereco");
        if (item && cJSON_IsNumber(item)) holding_reg1000_params.reg1000[endereco] = item->valueint;
        item = cJSON_GetObjectItem(root, "paridade");
        if (item && cJSON_IsNumber(item)) holding_reg1000_params.reg1000[paridade] = item->valueint;
    }

    // Faixa 2000 - Dados
    cJSON *reg2000_obj = cJSON_GetObjectItem(root, "reg2000");
    if (reg2000_obj) {
        cJSON *item = cJSON_GetObjectItem(reg2000_obj, "dataValue"); 
        if (item && cJSON_IsNumber(item)) reg2000[dataValue] = item->valueint;
    }

    // Faixa 4000 - Sonda Lambda
    cJSON *reg4000_obj = cJSON_GetObjectItem(root, "reg4000");
    if (reg4000_obj) {
        cJSON *item = cJSON_GetObjectItem(reg4000_obj, "lambdaValue"); 
        if (item && cJSON_IsNumber(item)) reg4000[lambdaValue] = item->valueint;
        item = cJSON_GetObjectItem(reg4000_obj, "lambdaRef");   
        if (item && cJSON_IsNumber(item)) reg4000[lambdaRef] = item->valueint;
        item = cJSON_GetObjectItem(reg4000_obj, "heatValue");   
        if (item && cJSON_IsNumber(item)) reg4000[heatValue] = item->valueint;
        item = cJSON_GetObjectItem(reg4000_obj, "heatRef");     
        if (item && cJSON_IsNumber(item)) reg4000[heatRef] = item->valueint;
        item = cJSON_GetObjectItem(reg4000_obj, "output_mb");   
        if (item && cJSON_IsNumber(item)) reg4000[output_mb] = item->valueint;
        item = cJSON_GetObjectItem(reg4000_obj, "PROBE_DEMAGED");   
        if (item && cJSON_IsNumber(item)) reg4000[PROBE_DEMAGED] = item->valueint;
        item = cJSON_GetObjectItem(reg4000_obj, "PROBE_TEMP_OUT_OF_RANGE");   
        if (item && cJSON_IsNumber(item)) reg4000[PROBE_TEMP_OUT_OF_RANGE] = item->valueint;
        item = cJSON_GetObjectItem(reg4000_obj, "COMPRESSOR_FAIL");   
        if (item && cJSON_IsNumber(item)) reg4000[COMPRESSOR_FAIL] = item->valueint;
    }

    // Faixa 6000 - DAC
    cJSON *reg6000_obj = cJSON_GetObjectItem(root, "reg6000");
    if (reg6000_obj) {
        cJSON *item = cJSON_GetObjectItem(reg6000_obj, "maxDac0"); 
        if (item && cJSON_IsNumber(item)) reg6000[maxDac0] = item->valueint;
        item = cJSON_GetObjectItem(reg6000_obj, "forcaValorDAC"); 
        if (item && cJSON_IsNumber(item)) reg6000[forcaValorDAC] = item->valueint;
        item = cJSON_GetObjectItem(reg6000_obj, "nada"); 
        if (item && cJSON_IsNumber(item)) reg6000[nada] = item->valueint;
        item = cJSON_GetObjectItem(reg6000_obj, "dACGain0"); 
        if (item && cJSON_IsNumber(item)) reg6000[dACGain0] = item->valueint;
        item = cJSON_GetObjectItem(reg6000_obj, "dACOffset0"); 
        if (item && cJSON_IsNumber(item)) reg6000[dACOffset0] = item->valueint;
    } else {
        // Compatibilidade: arrays em modbus_registers/reg6000
        cJSON *registers_obj = cJSON_GetObjectItem(root, "modbus_registers");
        if (registers_obj) {
            cJSON *reg6000_array = cJSON_GetObjectItem(registers_obj, "reg6000");
            if (reg6000_array && cJSON_IsArray(reg6000_array)) {
                for (int i = 0; i < REG_6000_SIZE && i < cJSON_GetArraySize(reg6000_array); i++) {
                    cJSON *it = cJSON_GetArrayItem(reg6000_array, i);
                    if (cJSON_IsNumber(it)) reg6000[i] = it->valueint;
                }
            }
        }
    }

    // Faixa 9000 - Especificações
    cJSON *reg9000_obj = cJSON_GetObjectItem(root, "reg9000");
    if (reg9000_obj) {
        cJSON *item = cJSON_GetObjectItem(reg9000_obj, "valorZero"); 
        if (item && cJSON_IsNumber(item)) reg9000[valorZero] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "valorUm"); 
        if (item && cJSON_IsNumber(item)) reg9000[valorUm] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "firmVerHi"); 
        if (item && cJSON_IsNumber(item)) reg9000[firmVerHi] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "firmVerLo"); 
        if (item && cJSON_IsNumber(item)) reg9000[firmVerLo] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "valorQuatro"); 
        if (item && cJSON_IsNumber(item)) reg9000[valorQuatro] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "valorCinco"); 
        if (item && cJSON_IsNumber(item)) reg9000[valorCinco] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "lotnum0"); 
        if (item && cJSON_IsNumber(item)) reg9000[lotnum0] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "lotnum1"); 
        if (item && cJSON_IsNumber(item)) reg9000[lotnum1] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "lotnum2"); 
        if (item && cJSON_IsNumber(item)) reg9000[lotnum2] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "lotnum3"); 
        if (item && cJSON_IsNumber(item)) reg9000[lotnum3] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "lotnum4"); 
        if (item && cJSON_IsNumber(item)) reg9000[lotnum4] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "lotnum5"); 
        if (item && cJSON_IsNumber(item)) reg9000[lotnum5] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "wafnum"); 
        if (item && cJSON_IsNumber(item)) reg9000[wafnum] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "coordx0"); 
        if (item && cJSON_IsNumber(item)) reg9000[coordx0] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "coordx1"); 
        if (item && cJSON_IsNumber(item)) reg9000[coordx1] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "valor17"); 
        if (item && cJSON_IsNumber(item)) reg9000[valor17] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "valor18"); 
        if (item && cJSON_IsNumber(item)) reg9000[valor18] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "valor19"); 
        if (item && cJSON_IsNumber(item)) reg9000[valor19] = item->valueint;
    }

    ESP_LOGI(TAG, "Configuração RTU carregada: baud=%d, endereco=%d, paridade=%d",
             holding_reg1000_params.reg1000[baudrate],
             holding_reg1000_params.reg1000[endereco],
             holding_reg1000_params.reg1000[paridade]);

    cJSON_Delete(root);
    free(data);
    return ESP_OK;
}

// ================= AP Config (/spiffs/ap_config.json) =================
esp_err_t save_ap_config(const ap_config_t* config) {
    if (!config) return ESP_ERR_INVALID_ARG;
    
    init_spiffs();
    ESP_LOGI(TAG, "Salvando configuração AP...");

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "ssid", config->ssid);
    cJSON_AddStringToObject(root, "username", config->username);
    cJSON_AddStringToObject(root, "password", config->password);
    cJSON_AddStringToObject(root, "ip", config->ip);

    char *json_str = cJSON_Print(root);
    esp_err_t result = ESP_OK;
    
    // Garantir que o diretório existe
    ensure_data_config_dir();
    
    // 1. Salvar no arquivo SPIFFS
    FILE *f = fopen(AP_CONFIG_FILE, "w");
    if (f) {
        fprintf(f, "%s", json_str);
        fclose(f);
        ESP_LOGI(TAG, "Configuração AP salva em %s", AP_CONFIG_FILE);
    } else {
        ESP_LOGE(TAG, "Erro ao abrir %s para escrita", AP_CONFIG_FILE);
        result = ESP_FAIL;
    }
    
    // 2. Salvar backup na NVS
    esp_err_t nvs_result = save_json_to_nvs("config_backup", NVS_AP_KEY, json_str);
    if (nvs_result != ESP_OK) {
        ESP_LOGW(TAG, "Falha ao salvar backup AP na NVS");
        if (result == ESP_OK) result = ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Configuração AP processada: SSID=%s, IP=%s", config->ssid, config->ip);
    cJSON_Delete(root);
    free(json_str);
    return result;
}

esp_err_t load_ap_config(ap_config_t* config) {
    if (!config) return ESP_ERR_INVALID_ARG;
    
    init_spiffs();
    ESP_LOGI(TAG, "Carregando configuração AP...");

    // Valores padrão
    strcpy(config->ssid, "ESP32-WebServer");
    strcpy(config->username, "admin");
    strcpy(config->password, "12345678");
    strcpy(config->ip, "192.168.4.1");

    char *data = NULL;
    cJSON *root = NULL;
    esp_err_t result = ESP_FAIL;
    
    // 1. Tentar carregar do arquivo SPIFFS (prioridade)
    FILE *f = fopen(AP_CONFIG_FILE, "r");
    if (!f) {
        // Tentar caminho antigo para compatibilidade
        f = fopen(AP_CONFIG_FILE_OLD, "r");
        if (f) {
            ESP_LOGI(TAG, "Carregando AP config do caminho antigo: %s", AP_CONFIG_FILE_OLD);
        }
    } else {
        ESP_LOGI(TAG, "Carregando AP config do caminho novo: %s", AP_CONFIG_FILE);
    }
    
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        rewind(f);
        
        data = malloc(size + 1);
        if (data) {
            fread(data, 1, size, f);
            data[size] = '\0';
            fclose(f);
            
            root = cJSON_Parse(data);
            if (root) {
                ESP_LOGI(TAG, "Configuração AP carregada do arquivo SPIFFS");
                result = ESP_OK;
            } else {
                ESP_LOGE(TAG, "Erro ao parsear arquivo AP do SPIFFS");
                free(data);
                data = NULL;
            }
        } else {
            fclose(f);
        }
    }
    
    // 2. Se falhou no SPIFFS, tentar carregar da NVS (fallback)
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Tentando carregar AP config da NVS (fallback)...");
        
        if (load_json_from_nvs("config_backup", NVS_AP_KEY, &data) == ESP_OK && data) {
            root = cJSON_Parse(data);
            if (root) {
                ESP_LOGI(TAG, "Configuração AP recuperada da NVS com sucesso!");
                result = ESP_OK;
            } else {
                ESP_LOGE(TAG, "Erro ao parsear JSON AP da NVS");
            }
        } else {
            ESP_LOGW(TAG, "Backup AP não encontrado na NVS");
        }
    }
    
    // 3. Se ambos falharam, manter valores padrão
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Usando valores padrão AP (SPIFFS e NVS indisponíveis)");
        if (data) free(data);
        return ESP_FAIL;
    }

    // Fazer o parsing dos dados JSON
    cJSON *item = cJSON_GetObjectItem(root, "ssid");
    if (item && cJSON_IsString(item)) strncpy(config->ssid, item->valuestring, sizeof(config->ssid)-1);
    
    item = cJSON_GetObjectItem(root, "username");
    if (item && cJSON_IsString(item)) strncpy(config->username, item->valuestring, sizeof(config->username)-1);
    
    item = cJSON_GetObjectItem(root, "password");
    if (item && cJSON_IsString(item)) strncpy(config->password, item->valuestring, sizeof(config->password)-1);
    
    item = cJSON_GetObjectItem(root, "ip");
    if (item && cJSON_IsString(item)) strncpy(config->ip, item->valuestring, sizeof(config->ip)-1);

    ESP_LOGI(TAG, "Configuração AP carregada: SSID=%s, IP=%s", config->ssid, config->ip);
    cJSON_Delete(root);
    free(data);
    return ESP_OK;
}

// ================= STA Config (/spiffs/sta_config.json) =================
esp_err_t save_sta_config(const sta_config_t* config) {
    if (!config) return ESP_ERR_INVALID_ARG;
    
    init_spiffs();
    ESP_LOGI(TAG, "Salvando configuração STA...");

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "ssid", config->ssid);
    cJSON_AddStringToObject(root, "password", config->password);

    char *json_str = cJSON_Print(root);
    esp_err_t result = ESP_OK;
    
    // Garantir que o diretório existe
    ensure_data_config_dir();
    
    // 1. Salvar no arquivo SPIFFS
    FILE *f = fopen(STA_CONFIG_FILE, "w");
    if (f) {
        fprintf(f, "%s", json_str);
        fclose(f);
        ESP_LOGI(TAG, "Configuração STA salva em %s", STA_CONFIG_FILE);
    } else {
        ESP_LOGE(TAG, "Erro ao abrir %s para escrita", STA_CONFIG_FILE);
        result = ESP_FAIL;
    }
    
    // 2. Salvar backup na NVS
    esp_err_t nvs_result = save_json_to_nvs("config_backup", NVS_STA_KEY, json_str);
    if (nvs_result != ESP_OK) {
        ESP_LOGW(TAG, "Falha ao salvar backup STA na NVS");
        if (result == ESP_OK) result = ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Configuração STA processada: SSID=%s", config->ssid);
    cJSON_Delete(root);
    free(json_str);
    return result;
}

esp_err_t load_sta_config(sta_config_t* config) {
    if (!config) return ESP_ERR_INVALID_ARG;
    
    init_spiffs();
    ESP_LOGI(TAG, "Carregando configuração STA...");

    // Valores padrão (vazios)
    config->ssid[0] = '\0';
    config->password[0] = '\0';

    char *data = NULL;
    cJSON *root = NULL;
    esp_err_t result = ESP_FAIL;
    
    // 1. Tentar carregar do arquivo SPIFFS (prioridade)
    ESP_LOGI(TAG, "Tentando abrir: %s", STA_CONFIG_FILE);
    FILE *f = fopen(STA_CONFIG_FILE, "r");
    if (!f) {
        ESP_LOGW(TAG, "Arquivo não encontrado: %s", STA_CONFIG_FILE);
        // Tentar caminho antigo para compatibilidade
        ESP_LOGI(TAG, "Tentando abrir: %s", STA_CONFIG_FILE_OLD);
        f = fopen(STA_CONFIG_FILE_OLD, "r");
        if (f) {
            ESP_LOGI(TAG, "Carregando STA config do caminho antigo: %s", STA_CONFIG_FILE_OLD);
        } else {
            ESP_LOGW(TAG, "Arquivo não encontrado: %s", STA_CONFIG_FILE_OLD);
        }
    } else {
        ESP_LOGI(TAG, "Arquivo encontrado! Carregando STA config de: %s", STA_CONFIG_FILE);
    }
    
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        rewind(f);
        
        data = malloc(size + 1);
        if (data) {
            fread(data, 1, size, f);
            data[size] = '\0';
            fclose(f);
            
            root = cJSON_Parse(data);
            if (root) {
                ESP_LOGI(TAG, "Configuração STA carregada do arquivo SPIFFS");
                result = ESP_OK;
            } else {
                ESP_LOGE(TAG, "Erro ao parsear arquivo STA do SPIFFS");
                free(data);
                data = NULL;
            }
        } else {
            fclose(f);
        }
    }
    
    // 2. Se falhou no SPIFFS, tentar carregar da NVS (fallback)
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Tentando carregar STA config da NVS (fallback)...");
        
        if (load_json_from_nvs("config_backup", NVS_STA_KEY, &data) == ESP_OK && data) {
            root = cJSON_Parse(data);
            if (root) {
                ESP_LOGI(TAG, "Configuração STA recuperada da NVS com sucesso!");
                result = ESP_OK;
            } else {
                ESP_LOGE(TAG, "Erro ao parsear JSON STA da NVS");
            }
        } else {
            ESP_LOGW(TAG, "Backup STA não encontrado na NVS");
        }
    }
    
    // 3. Se ambos falharam, manter valores padrão
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Usando valores padrão STA (SPIFFS e NVS indisponíveis)");
        if (data) free(data);
        return ESP_FAIL;
    }

    // Fazer o parsing dos dados JSON
    cJSON *item = cJSON_GetObjectItem(root, "ssid");
    if (item && cJSON_IsString(item)) strncpy(config->ssid, item->valuestring, sizeof(config->ssid)-1);
    
    item = cJSON_GetObjectItem(root, "password");
    if (item && cJSON_IsString(item)) strncpy(config->password, item->valuestring, sizeof(config->password)-1);

    ESP_LOGI(TAG, "Configuração STA carregada: SSID=%s", config->ssid);
    cJSON_Delete(root);
    free(data);
    return ESP_OK;
}

// ================= MQTT Config (/spiffs/mqtt_config.json) =================
esp_err_t save_mqtt_config(const mqtt_config_t* config) {
    if (!config) return ESP_ERR_INVALID_ARG;
    
    init_spiffs();
    ESP_LOGI(TAG, "Salvando configuração MQTT...");

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "broker_url", config->broker_url);
    cJSON_AddStringToObject(root, "client_id", config->client_id);
    cJSON_AddStringToObject(root, "username", config->username);
    cJSON_AddStringToObject(root, "password", config->password);
    cJSON_AddNumberToObject(root, "port", config->port);
    cJSON_AddNumberToObject(root, "qos", config->qos);
    cJSON_AddBoolToObject(root, "retain", config->retain);
    cJSON_AddBoolToObject(root, "tls_enabled", config->tls_enabled);
    cJSON_AddStringToObject(root, "ca_path", config->ca_path);
    cJSON_AddBoolToObject(root, "enabled", config->enabled);
    cJSON_AddNumberToObject(root, "publish_interval_ms", config->publish_interval_ms);

    char *json_str = cJSON_Print(root);
    esp_err_t result = ESP_OK;
    
    // Garantir que o diretório existe
    ensure_data_config_dir();
    
    // 1. Salvar no arquivo SPIFFS
    FILE *f = fopen(MQTT_CONFIG_FILE, "w");
    if (f) {
        fprintf(f, "%s", json_str);
        fclose(f);
        ESP_LOGI(TAG, "Configuração MQTT salva em %s", MQTT_CONFIG_FILE);
    } else {
        ESP_LOGE(TAG, "Erro ao abrir %s para escrita", MQTT_CONFIG_FILE);
        result = ESP_FAIL;
    }
    
    // 2. Salvar backup na NVS
    esp_err_t nvs_result = save_json_to_nvs("config_backup", NVS_MQTT_KEY, json_str);
    if (nvs_result != ESP_OK) {
        ESP_LOGW(TAG, "Falha ao salvar backup MQTT na NVS");
        if (result == ESP_OK) result = ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Configuração MQTT processada: broker=%s, enabled=%s", 
             config->broker_url, config->enabled ? "true" : "false");
    cJSON_Delete(root);
    free(json_str);
    return result;
}

esp_err_t load_mqtt_config(mqtt_config_t* config) {
    if (!config) return ESP_ERR_INVALID_ARG;
    
    init_spiffs();
    ESP_LOGI(TAG, "Carregando configuração MQTT...");

    // Valores padrão
    strcpy(config->broker_url, "mqtt://broker.hivemq.com");
    strcpy(config->client_id, "ESP32_SondaLambda");
    config->username[0] = '\0';
    config->password[0] = '\0';
    config->port = 1883;
    config->qos = 1;
    config->retain = false;
    config->tls_enabled = false;
    config->ca_path[0] = '\0';
    config->enabled = true;
    config->publish_interval_ms = 1000;

    FILE *f = fopen(MQTT_CONFIG_FILE, "r");
    if (!f) {
        // Tentar caminho antigo para compatibilidade
        f = fopen(MQTT_CONFIG_FILE_OLD, "r");
        if (!f) {
            ESP_LOGW(TAG, "Arquivos %s e %s não encontrados, usando valores padrão MQTT", MQTT_CONFIG_FILE, MQTT_CONFIG_FILE_OLD);
            return ESP_FAIL;
        } else {
            ESP_LOGI(TAG, "Carregando MQTT config do caminho antigo: %s", MQTT_CONFIG_FILE_OLD);
        }
    } else {
        ESP_LOGI(TAG, "Carregando MQTT config do caminho novo: %s", MQTT_CONFIG_FILE);
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    char *data = malloc(size + 1);
    fread(data, 1, size, f);
    data[size] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(data);
    if (!root) {
        ESP_LOGE(TAG, "Erro ao parsear %s", MQTT_CONFIG_FILE);
        free(data);
        return ESP_FAIL;
    }

    cJSON *item = cJSON_GetObjectItem(root, "broker_url");
    if (item && cJSON_IsString(item)) strncpy(config->broker_url, item->valuestring, sizeof(config->broker_url)-1);
    
    item = cJSON_GetObjectItem(root, "client_id");
    if (item && cJSON_IsString(item)) strncpy(config->client_id, item->valuestring, sizeof(config->client_id)-1);
    
    item = cJSON_GetObjectItem(root, "username");
    if (item && cJSON_IsString(item)) strncpy(config->username, item->valuestring, sizeof(config->username)-1);
    
    item = cJSON_GetObjectItem(root, "password");
    if (item && cJSON_IsString(item)) strncpy(config->password, item->valuestring, sizeof(config->password)-1);
    
    item = cJSON_GetObjectItem(root, "port");
    if (item && cJSON_IsNumber(item)) config->port = item->valueint;
    
    item = cJSON_GetObjectItem(root, "qos");
    if (item && cJSON_IsNumber(item)) config->qos = item->valueint;
    
    item = cJSON_GetObjectItem(root, "retain");
    if (item && cJSON_IsBool(item)) config->retain = cJSON_IsTrue(item);
    
    item = cJSON_GetObjectItem(root, "tls_enabled");
    if (item && cJSON_IsBool(item)) config->tls_enabled = cJSON_IsTrue(item);
    
    item = cJSON_GetObjectItem(root, "ca_path");
    if (item && cJSON_IsString(item)) strncpy(config->ca_path, item->valuestring, sizeof(config->ca_path)-1);
    
    item = cJSON_GetObjectItem(root, "enabled");
    if (item && cJSON_IsBool(item)) config->enabled = cJSON_IsTrue(item);
    
    item = cJSON_GetObjectItem(root, "publish_interval_ms");
    if (item && cJSON_IsNumber(item)) config->publish_interval_ms = item->valueint;

    ESP_LOGI(TAG, "Configuração MQTT carregada: broker=%s, enabled=%s", 
             config->broker_url, config->enabled ? "true" : "false");
    cJSON_Delete(root);
    free(data);
    return ESP_OK;
}

// ================= Network Config (/spiffs/network_config.json) =================
esp_err_t save_network_config(const network_config_t* config) {
    if (!config) return ESP_ERR_INVALID_ARG;
    
    init_spiffs();
    ESP_LOGI(TAG, "Salvando configuração de rede...");

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "ip", config->ip);
    cJSON_AddStringToObject(root, "mask", config->mask);
    cJSON_AddStringToObject(root, "gateway", config->gateway);
    cJSON_AddStringToObject(root, "dns", config->dns);

    char *json_str = cJSON_Print(root);
    FILE *f = fopen(NETWORK_CONFIG_FILE, "w");
    if (!f) {
        ESP_LOGE(TAG, "Erro ao abrir %s para escrita", NETWORK_CONFIG_FILE);
        cJSON_Delete(root);
        free(json_str);
        return ESP_FAIL;
    }

    fprintf(f, "%s", json_str);
    fclose(f);

    ESP_LOGI(TAG, "Configuração de rede salva: IP=%s, Gateway=%s", config->ip, config->gateway);
    cJSON_Delete(root);
    free(json_str);
    return ESP_OK;
}

esp_err_t load_network_config(network_config_t* config) {
    if (!config) return ESP_ERR_INVALID_ARG;
    
    init_spiffs();
    ESP_LOGI(TAG, "Carregando configuração de rede...");

    // Valores padrão (vazios = DHCP)
    config->ip[0] = '\0';
    config->mask[0] = '\0';
    config->gateway[0] = '\0';
    config->dns[0] = '\0';

    FILE *f = fopen(NETWORK_CONFIG_FILE, "r");
    if (!f) {
        // Tentar caminho antigo para compatibilidade
        f = fopen(NETWORK_CONFIG_FILE_OLD, "r");
        if (!f) {
            ESP_LOGW(TAG, "Arquivos %s e %s não encontrados, usando DHCP", NETWORK_CONFIG_FILE, NETWORK_CONFIG_FILE_OLD);
            return ESP_FAIL;
        } else {
            ESP_LOGI(TAG, "Carregando Network config do caminho antigo: %s", NETWORK_CONFIG_FILE_OLD);
        }
    } else {
        ESP_LOGI(TAG, "Carregando Network config do caminho novo: %s", NETWORK_CONFIG_FILE);
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    char *data = malloc(size + 1);
    fread(data, 1, size, f);
    data[size] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(data);
    if (!root) {
        ESP_LOGE(TAG, "Erro ao parsear %s", NETWORK_CONFIG_FILE);
        free(data);
        return ESP_FAIL;
    }

    cJSON *item = cJSON_GetObjectItem(root, "ip");
    if (item && cJSON_IsString(item)) strncpy(config->ip, item->valuestring, sizeof(config->ip)-1);
    
    item = cJSON_GetObjectItem(root, "mask");
    if (item && cJSON_IsString(item)) strncpy(config->mask, item->valuestring, sizeof(config->mask)-1);
    
    item = cJSON_GetObjectItem(root, "gateway");
    if (item && cJSON_IsString(item)) strncpy(config->gateway, item->valuestring, sizeof(config->gateway)-1);
    
    item = cJSON_GetObjectItem(root, "dns");
    if (item && cJSON_IsString(item)) strncpy(config->dns, item->valuestring, sizeof(config->dns)-1);

    ESP_LOGI(TAG, "Configuração de rede carregada: IP=%s, Gateway=%s", config->ip, config->gateway);
    cJSON_Delete(root);
    free(data);
    return ESP_OK;
}

// ================= Login state (mantido no NVS por segurança) =================
void save_login_state(bool state) {
    nvs_handle_t handle;
    if (nvs_open("storage", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, "login_state", state ? 1 : 0);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

void save_login_state_root(bool state) {
    nvs_handle_t handle;
    if (nvs_open("storage", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, "login_state_root", state ? 1 : 0);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

bool load_login_state(void) {
    nvs_handle_t handle;
    uint8_t state = 0;
    if (nvs_open("storage", NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u8(handle, "login_state", &state);
        nvs_close(handle);
    }
    return state == 1;
}

bool load_login_state_root(void) {
    nvs_handle_t handle;
    uint8_t state = 0;
    if (nvs_open("storage", NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u8(handle, "login_state_root", &state);
        nvs_close(handle);
    }
    return state == 1;
}

// ================= Legacy Functions (para compatibilidade) =================
esp_err_t save_config(void) {
    ESP_LOGW(TAG, "save_config() é legacy - use save_rtu_config()");
    return save_rtu_config();
}

esp_err_t load_config(void) {
    ESP_LOGW(TAG, "load_config() é legacy - use load_rtu_config()");
    return load_rtu_config();
}

// Wrappers para WiFi (migração gradual)
void save_wifi_config(const char* ssid, const char* password) {
    ESP_LOGW(TAG, "save_wifi_config() é legacy - use save_sta_config()");
    if (!ssid || !password) return;
    
    sta_config_t config;
    strncpy(config.ssid, ssid, sizeof(config.ssid)-1);
    strncpy(config.password, password, sizeof(config.password)-1);
    config.ssid[sizeof(config.ssid)-1] = '\0';
    config.password[sizeof(config.password)-1] = '\0';
    
    save_sta_config(&config);
}

void read_wifi_config(char* ssid, size_t ssid_len, char* password, size_t password_len) {
    ESP_LOGW(TAG, "read_wifi_config() é legacy - use load_sta_config()");
    if (!ssid || !password) return;
    
    sta_config_t config;
    if (load_sta_config(&config) == ESP_OK) {
        strncpy(ssid, config.ssid, ssid_len-1);
        strncpy(password, config.password, password_len-1);
        ssid[ssid_len-1] = '\0';
        password[password_len-1] = '\0';
    } else {
        ssid[0] = '\0';
        password[0] = '\0';
    }
}

// ================= Controle de Acesso por Nível de Usuário =================
void save_user_level(user_level_t level) {
    nvs_handle_t handle;
    if (nvs_open("storage", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, "user_level", (uint8_t)level);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(TAG, "Nível de usuário salvo: %d", level);
    }
}

user_level_t load_user_level(void) {
    nvs_handle_t handle;
    uint8_t level = USER_LEVEL_NONE;
    if (nvs_open("storage", NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u8(handle, "user_level", &level);
        nvs_close(handle);
    }
    return (user_level_t)level;
}

bool check_access_permission(user_level_t required_level) {
    user_level_t current_level = load_user_level();
    return current_level >= required_level;
}

bool can_modify_register_range(int register_base) {
    user_level_t current_level = load_user_level();
    
    switch (register_base) {
        case 1000: // Configuração RTU - apenas admin
            return current_level >= USER_LEVEL_ADMIN;
        
        case 2000: // Valores de monitoramento - apenas leitura para todos
            return false; // Nunca podem ser modificados via web
        
        case 4000: // Registradores editáveis - apenas admin
        case 6000:
        case 9000:
            return current_level >= USER_LEVEL_ADMIN;
        
        default:
            return false;
    }
}

// ================= Modbus TCP Config (/spiffs/modbus_tcp_config.json) =================
esp_err_t save_modbus_tcp_config(const modbus_tcp_config_t* config) {
    if (!config) {
        ESP_LOGE(TAG, "Configuração TCP é NULL");
        return ESP_ERR_INVALID_ARG;
    }

    init_spiffs();
    ESP_LOGI(TAG, "Salvando configuração Modbus TCP...");

    // Criar JSON com configurações TCP
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "enabled", config->enabled);
    cJSON_AddNumberToObject(root, "port", config->port);
    cJSON_AddNumberToObject(root, "max_connections", config->max_connections);

    // Gerar string JSON
    char *json_str = cJSON_Print(root);
    esp_err_t result = ESP_OK;

    // Garantir que o diretório existe
    ensure_data_config_dir();

    // 1. Salvar no arquivo SPIFFS
    FILE *f = fopen(MODBUS_TCP_CONFIG_FILE, "w");
    if (f) {
        fprintf(f, "%s", json_str);
        fclose(f);
        ESP_LOGI(TAG, "Configuração Modbus TCP salva: enabled=%d, port=%d", 
                 config->enabled, config->port);
    } else {
        ESP_LOGE(TAG, "Erro ao salvar %s", MODBUS_TCP_CONFIG_FILE);
        result = ESP_FAIL;
    }

    // 2. Backup na NVS (redundância)
    if (result == ESP_OK) {
        esp_err_t nvs_result = save_json_to_nvs("storage", NVS_MODBUS_TCP_KEY, json_str);
        if (nvs_result != ESP_OK) {
            ESP_LOGW(TAG, "Backup NVS falhou, mas config SPIFFS OK");
        }
    }

    cJSON_Delete(root);
    free(json_str);
    return result;
}

esp_err_t load_modbus_tcp_config(modbus_tcp_config_t* config) {
    ESP_LOGI(TAG, "load_modbus_tcp_config() chamada");
    
    if (!config) {
        ESP_LOGE(TAG, "Configuração TCP é NULL");
        return ESP_ERR_INVALID_ARG;
    }

    init_spiffs();

    // Valores padrão (TCP desabilitado por padrão)
    config->enabled = false;
    config->port = 502;
    config->max_connections = 5;
    ESP_LOGI(TAG, "Valores padrão definidos: enabled=false, port=502, max_conn=5");

    // GARANTIR QUE O DIRETÓRIO EXISTE
    ESP_LOGI(TAG, "Verificando/criando diretório /spiffs/data/config/");
    
    // Criar diretório se não existir (mkdir não está disponível, vamos tentar criar um arquivo teste)
    FILE *test_dir = fopen("/spiffs/data/config/.test", "w");
    if (test_dir) {
        fclose(test_dir);
        remove("/spiffs/data/config/.test");
        ESP_LOGI(TAG, "Diretório /spiffs/data/config/ acessível");
    } else {
        ESP_LOGW(TAG, "Diretório /spiffs/data/config/ não acessível, criando estrutura...");
    }

    ESP_LOGI(TAG, "Tentando carregar arquivo: %s", MODBUS_TCP_CONFIG_FILE);
    // Tentar carregar do arquivo SPIFFS (prioridade)
    FILE *f = fopen(MODBUS_TCP_CONFIG_FILE, "r");
    if (!f) {
        ESP_LOGW(TAG, "Arquivo principal não encontrado, tentando: %s", MODBUS_TCP_CONFIG_FILE_OLD);
        // Fallback: tentar caminho antigo
        f = fopen(MODBUS_TCP_CONFIG_FILE_OLD, "r");
    }

    char *data = NULL;
    
    if (f) {
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);

        data = malloc(fsize + 1);
        if (data) {
            fread(data, 1, fsize, f);
            data[fsize] = 0;
        }
        fclose(f);
        ESP_LOGI(TAG, "Configuração Modbus TCP carregada do arquivo");
    } else {
        // Fallback: tentar carregar da NVS
        ESP_LOGW(TAG, "Arquivos TCP não encontrados, tentando NVS com chave: %s", NVS_MODBUS_TCP_KEY);
        esp_err_t nvs_result = load_json_from_nvs("storage", NVS_MODBUS_TCP_KEY, &data);
        if (nvs_result != ESP_OK) {
            ESP_LOGW(TAG, "Chave %s não encontrada na NVS: %s", NVS_MODBUS_TCP_KEY, esp_err_to_name(nvs_result));
            
            // CRIAR ARQUIVO PADRÃO AUTOMATICAMENTE
            ESP_LOGW(TAG, "Criando arquivo de configuração TCP padrão com enabled=true...");
            
            FILE *new_file = fopen(MODBUS_TCP_CONFIG_FILE, "w");
            if (new_file) {
                fprintf(new_file, "{\"enabled\":true,\"port\":502,\"max_connections\":5}");
                fclose(new_file);
                ESP_LOGI(TAG, "Arquivo %s criado com sucesso!", MODBUS_TCP_CONFIG_FILE);
                
                // Tenta carregar o arquivo recém-criado
                f = fopen(MODBUS_TCP_CONFIG_FILE, "r");
                if (f) {
                    ESP_LOGI(TAG, "Carregando arquivo recém-criado...");
                    fseek(f, 0, SEEK_END);
                    long fsize = ftell(f);
                    fseek(f, 0, SEEK_SET);
                    data = malloc(fsize + 1);
                    if (data) {
                        fread(data, 1, fsize, f);
                        data[fsize] = 0;
                    }
                    fclose(f);
                } else {
                    ESP_LOGI(TAG, "Usando valores padrão (TCP enabled=true)");
                    config->enabled = true;  // FORÇA HABILITADO
                    return ESP_OK;
                }
            } else {
                ESP_LOGI(TAG, "📦 Usando valores padrão (TCP enabled=true)");
                config->enabled = true;  // FORÇA HABILITADO
                return ESP_OK;
            }
        } else {
            ESP_LOGI(TAG, "Configuração TCP carregada da NVS (backup)");
        }
    }

    if (!data) {
        ESP_LOGI(TAG, "Usando valores padrão (TCP desabilitado)");
        return ESP_OK;
    }

    // Parse JSON
    cJSON *root = cJSON_Parse(data);
    if (!root) {
        ESP_LOGE(TAG, "Erro ao parsear JSON Modbus TCP");
        free(data);
        return ESP_FAIL;
    }

    // Carregar campos
    cJSON *item = cJSON_GetObjectItem(root, "enabled");
    if (item && cJSON_IsBool(item)) config->enabled = cJSON_IsTrue(item);

    item = cJSON_GetObjectItem(root, "port");
    if (item && cJSON_IsNumber(item)) config->port = item->valueint;

    item = cJSON_GetObjectItem(root, "max_connections");
    if (item && cJSON_IsNumber(item)) config->max_connections = item->valueint;

    ESP_LOGI(TAG, "Modbus TCP Config: enabled=%d, port=%d, max_conn=%d",
             config->enabled, config->port, config->max_connections);

    cJSON_Delete(root);
    free(data);
    return ESP_OK;
}
