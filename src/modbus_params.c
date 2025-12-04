/* ============================================================================
 * MODBUS_PARAMS.C - DEFINIÇÕES E GERENCIAMENTO DE REGISTRADORES MODBUS
 * ============================================================================
 * 
 * DESCRIÇÃO:
 * -----------
 * Este arquivo define e gerencia todos os registradores Modbus do sistema,
 * incluindo coils, discrete inputs, holding registers e input registers.
 * Implementa proteção thread-safe via mutex para acesso seguro aos 
 * registradores por múltiplas tasks simultaneamente.
 * 
 * MAPEAMENTO DE REGISTRADORES:
 * ----------------------------
 * 
 * HOLDING REGISTERS (Leitura/Escrita):
 * ┌─────────┬──────────────────┬────────────────────────────────────────┐
 * │ Faixa   │ Nome             │ Conteúdo                               │
 * ├─────────┼──────────────────┼────────────────────────────────────────┤
 * │ 1000    │ reg1000          │ Configuração RTU (baud, endereço, etc)│
 * │ 2000    │ reg2000          │ Dados de monitoramento (só leitura)   │
 * │ 4000    │ reg4000          │ Sonda Lambda (valores e status)       │
 * │ 6000    │ reg6000          │ Configuração DAC e controle           │
 * │ 9000    │ reg9000          │ Especificações unitárias e firmware   │
 * └─────────┴──────────────────┴────────────────────────────────────────┘
 * 
 * 🔘 COILS (Leitura/Escrita - 1 bit):
 * - discrete outputs controláveis remotamente
 * 
 * 📍 DISCRETE INPUTS (Só leitura - 1 bit):  
 * - entradas digitais do sistema
 * 
 * 📈 INPUT REGISTERS (Só leitura - 16 bits):
 * - valores analógicos e status do sistema
 * 
 * PROTEÇÃO THREAD-SAFE:
 * ----------------------
 * Todos os acessos aos registradores devem ser protegidos pelo mutex:
 * 
 * ```c
 * if (modbus_lock_registers(1000)) {    // Lock com timeout de 1s
 *     reg4000[lambdaValue] = novo_valor;  // Operação thread-safe
 *     modbus_unlock_registers();          // Sempre fazer unlock!
 * }
 * ```
 * 
 * DEPENDÊNCIAS:
 * -------------
 * - include/modbus_params.h      : Declarações e índices dos registradores
 * - src/config_manager.c         : Carrega/salva configurações em JSON
 * - src/modbus_slave_task.c      : Acessa registradores via protocolo Modbus
 * - src/oxygen_sensor_task.c     : Atualiza reg4000 (sonda) e reg6000 (DAC)
 * - webserver.c                  : Acesso web aos registradores via HTTP
 * 
 * UTILIZAÇÃO PRINCIPAL:
 * ---------------------
 * 1. modbus_slave_task.c: Resposta a comandos Modbus RTU/TCP
 * 2. oxygen_sensor_task.c: Atualização contínua de valores da sonda
 * 3. webserver.c: Interface web para monitoramento e configuração
 * 4. config_manager.c: Persistência de configurações no NVS/SPIFFS
 * 
 * INICIALIZAÇÃO:
 * --------------
 * 1. modbus_mutex_init() - cria mutex de proteção
 * 2. Inicialização dos valores padrão dos registradores
 * 3. load_*_from_nvs() - carrega valores salvos (se existirem)
 * 
 * EXEMPLO DE ACESSO SEGURO:
 * -------------------------
 * ```c
 * // Leitura thread-safe
 * uint16_t valor_lambda = 0;
 * if (modbus_lock_registers(500)) {
 *     valor_lambda = reg4000[lambdaValue];
 *     modbus_unlock_registers();
 * }
 * 
 * // Escrita thread-safe
 * if (modbus_lock_registers(500)) {
 *     reg4000[lambdaRef] = 1000;  // Nova referência
 *     modbus_unlock_registers();
 * }
 * ```
 * 
 * ============================================================================
 */

#include "modbus_params.h"
#include "esp_log.h"

static const char *TAG = "MODBUS_PARAMS";

// Definições globais reais (alocação de memória)
discrete_reg_params_t discrete_reg_params;
holding_reg_params_t  holding_reg_params;
holding_reg1000_params_t  holding_reg1000_params;
uint16_t reg2000[REG_DATA_SIZE]; //2000
uint16_t reg3000[REG_3000_SIZE];
uint16_t reg4000[REG_4000_SIZE];
uint16_t reg5000[REG_5000_SIZE];
uint16_t reg6000[REG_6000_SIZE];
uint16_t reg7000[REG_7000_SIZE];
uint16_t reg8000[REG_8000_SIZE];
uint16_t reg9000[REG_UNITSPECS_SIZE];
coil_reg_params_t     coil_reg_params;
input_reg_params_t    input_reg_params;

// ================= VARIÁVEL DE CONTROLE RTU ENABLED =================
// Controla se o Modbus RTU está habilitado ou desabilitado
bool modbus_rtu_enabled = true;  // Habilitado por padrão

// ================= MUTEX DE PROTEÇÃO DOS REGISTRADORES =================
SemaphoreHandle_t modbus_registers_mutex = NULL;

bool modbus_mutex_init(void) {
    if (modbus_registers_mutex != NULL) {
        ESP_LOGW(TAG, "Mutex já inicializado");
        return true;
    }

    modbus_registers_mutex = xSemaphoreCreateMutex();
    if (modbus_registers_mutex == NULL) {
        ESP_LOGE(TAG, "Falha ao criar mutex de registradores Modbus");
        return false;
    }

    ESP_LOGI(TAG, "Mutex de registradores Modbus criado com sucesso");
    return true;
}

bool modbus_lock_registers(uint32_t timeout_ms) {
    if (modbus_registers_mutex == NULL) {
        ESP_LOGE(TAG, "Mutex não inicializado! Chame modbus_mutex_init() primeiro");
        return false;
    }

    TickType_t timeout_ticks = (timeout_ms == portMAX_DELAY) 
                                ? portMAX_DELAY 
                                : pdMS_TO_TICKS(timeout_ms);

    if (xSemaphoreTake(modbus_registers_mutex, timeout_ticks) == pdTRUE) {
        return true;
    }

    ESP_LOGW(TAG, "Timeout ao tentar lock nos registradores Modbus");
    return false;
}

void modbus_unlock_registers(void) {
    if (modbus_registers_mutex == NULL) {
        ESP_LOGE(TAG, "Mutex não inicializado!");
        return;
    }

    xSemaphoreGive(modbus_registers_mutex);
}

void modbus_mutex_destroy(void) {
    if (modbus_registers_mutex != NULL) {
        vSemaphoreDelete(modbus_registers_mutex);
        modbus_registers_mutex = NULL;
        ESP_LOGI(TAG, "🗑️ Mutex de registradores Modbus destruído");
    }
}