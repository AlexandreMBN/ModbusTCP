/**
 * @file modbus_register_sync.c
 * @brief Implementação da sincronização de registradores entre RTU e TCP
 * 
 * Este módulo implementa a sincronização bidirecional de todos os registradores
 * Modbus entre a implementação RTU (ESP-IDF nativo) e TCP (biblioteca customizada).
 * 
 * ESTRATÉGIA DE SINCRONIZAÇÃO:
 * ----------------------------
 * 1. Registradores compartilhados (modbus_params.h) servem como "fonte da verdade"
 * 2. RTU → TCP: Lê da implementação RTU e atualiza TCP
 * 3. TCP → RTU: Lê da implementação TCP e atualiza RTU
 * 4. Sincronização automática a cada 1s quando ambos ativos
 * 
 * TIPOS DE REGISTRADORES SINCRONIZADOS:
 * ------------------------------------
 * - Coils (0x01/0x05/0x0F)
 * - Discrete Inputs (0x02)  
 * - Input Registers (0x04)
 * - Holding Registers (0x03/0x06/0x10)
 * - Arrays customizados (reg2000, reg3000, reg4000, etc.)
 * 
 * @author Sistema ESP32
 * @date 2025
 */

#include "modbus_manager.h"
#include "modbus_params.h"
// #include "modbus_map.h"  // Já incluído via modbus_params.h - removido para evitar dupla inclusão
#include "modbus_tcp_slave.h"
#include "esp_log.h"
#include <string.h>

/* ============================================================================
 * CONSTANTES E DEFINIÇÕES
 * ============================================================================ */

static const char *TAG = "MODBUS_SYNC";

// Mapeamento de endereços (RTU → TCP)
#define RTU_TO_TCP_HOLDING_OFFSET    0    // Holding registers RTU começam em 0
#define RTU_TO_TCP_INPUT_OFFSET      0    // Input registers RTU começam em 0 
#define RTU_TO_TCP_COIL_OFFSET       0    // Coils RTU começam em 0
#define RTU_TO_TCP_DISCRETE_OFFSET   0    // Discrete inputs RTU começam em 0

/* ============================================================================
 * FUNÇÕES DE SINCRONIZAÇÃO: RTU → TCP
 * ============================================================================ */

/**
 * @brief Sincroniza holding registers básicos RTU → TCP
 */
static esp_err_t sync_holding_registers_rtu_to_tcp(modbus_tcp_handle_t tcp_handle) {
    ESP_LOGD(TAG, "📋 Sincronizando holding registers RTU → TCP");
    
    // Sincroniza holding_reg_params (registers 0-7)
    esp_err_t result = ESP_OK;
    
    // Registradores 0-7 (holding_reg_params) - Agora todos são float
    result |= modbus_tcp_set_holding_register(tcp_handle, 0, (uint16_t)holding_reg_params.holding_data0);
    result |= modbus_tcp_set_holding_register(tcp_handle, 1, (uint16_t)holding_reg_params.holding_data1);
    result |= modbus_tcp_set_holding_register(tcp_handle, 2, (uint16_t)holding_reg_params.holding_data2);
    result |= modbus_tcp_set_holding_register(tcp_handle, 3, (uint16_t)holding_reg_params.holding_data3);
    result |= modbus_tcp_set_holding_register(tcp_handle, 4, (uint16_t)holding_reg_params.holding_data4);
    result |= modbus_tcp_set_holding_register(tcp_handle, 5, (uint16_t)holding_reg_params.holding_data5);
    result |= modbus_tcp_set_holding_register(tcp_handle, 6, (uint16_t)holding_reg_params.holding_data6);
    result |= modbus_tcp_set_holding_register(tcp_handle, 7, (uint16_t)holding_reg_params.holding_data7);
    
    // Registradores 1000+ (configuração)
    for (int i = 0; i < REG_CONFIG_SIZE && i < 100; i++) {
        result |= modbus_tcp_set_holding_register(tcp_handle, 1000 + i, holding_reg1000_params.reg1000[i]);
    }
    
    // Array reg2000 (dados principais)
    for (int i = 0; i < REG_DATA_SIZE && i < 100; i++) {
        result |= modbus_tcp_set_holding_register(tcp_handle, REG_DATA_START + i, reg2000[i]);
    }
    
    // Array reg3000 (configurações DAC)
    for (int i = 0; i < REG_3000_SIZE && i < 100; i++) {
        result |= modbus_tcp_set_holding_register(tcp_handle, REG_3000_START + i, reg3000[i]);
    }
    
    // Array reg4000 (dados lambda)
    for (int i = 0; i < REG_4000_SIZE && i < 100; i++) {
        result |= modbus_tcp_set_holding_register(tcp_handle, REG_4000_START + i, reg4000[i]);
    }
    
    if (result == ESP_OK) {
        ESP_LOGD(TAG, "✅ Holding registers sincronizados RTU → TCP");
    } else {
        ESP_LOGW(TAG, "⚠️ Alguns holding registers falharam na sincronização RTU → TCP");
    }
    
    return result;
}

/**
 * @brief Sincroniza input registers RTU → TCP
 */
static esp_err_t sync_input_registers_rtu_to_tcp(modbus_tcp_handle_t tcp_handle) {
    ESP_LOGD(TAG, "📋 Sincronizando input registers RTU → TCP");
    
    esp_err_t result = ESP_OK;
    
    // Sincroniza input_reg_params (registers 0-7) - Todos são float
    result |= modbus_tcp_set_input_register(tcp_handle, 0, (uint16_t)input_reg_params.input_data0);
    result |= modbus_tcp_set_input_register(tcp_handle, 1, (uint16_t)input_reg_params.input_data1);
    result |= modbus_tcp_set_input_register(tcp_handle, 2, (uint16_t)input_reg_params.input_data2);
    result |= modbus_tcp_set_input_register(tcp_handle, 3, (uint16_t)input_reg_params.input_data3);
    result |= modbus_tcp_set_input_register(tcp_handle, 4, (uint16_t)input_reg_params.input_data4);
    result |= modbus_tcp_set_input_register(tcp_handle, 5, (uint16_t)input_reg_params.input_data5);
    result |= modbus_tcp_set_input_register(tcp_handle, 6, (uint16_t)input_reg_params.input_data6);
    result |= modbus_tcp_set_input_register(tcp_handle, 7, (uint16_t)input_reg_params.input_data7);
    
    if (result == ESP_OK) {
        ESP_LOGD(TAG, "✅ Input registers sincronizados RTU → TCP");
    } else {
        ESP_LOGW(TAG, "⚠️ Alguns input registers falharam na sincronização RTU → TCP");
    }
    
    return result;
}

/**
 * @brief Sincroniza coils RTU → TCP
 */
static esp_err_t sync_coils_rtu_to_tcp(modbus_tcp_handle_t tcp_handle) {
    ESP_LOGD(TAG, "📋 Sincronizando coils RTU → TCP");
    
    esp_err_t result = ESP_OK;
    
    // Sincroniza coil_reg_params
    uint8_t coil_port0 = coil_reg_params.coils_port0;
    uint8_t coil_port1 = coil_reg_params.coils_port1;
    
    // Expande bits individuais para coils separadas
    for (int i = 0; i < 8; i++) {
        bool coil_value = (coil_port0 >> i) & 0x01;
        result |= modbus_tcp_set_coil(tcp_handle, i, coil_value);
    }
    
    for (int i = 0; i < 8; i++) {
        bool coil_value = (coil_port1 >> i) & 0x01;
        result |= modbus_tcp_set_coil(tcp_handle, 8 + i, coil_value);
    }
    
    if (result == ESP_OK) {
        ESP_LOGD(TAG, "✅ Coils sincronizados RTU → TCP");
    } else {
        ESP_LOGW(TAG, "⚠️ Alguns coils falharam na sincronização RTU → TCP");
    }
    
    return result;
}

/**
 * @brief Sincroniza discrete inputs RTU → TCP
 */
static esp_err_t sync_discrete_inputs_rtu_to_tcp(modbus_tcp_handle_t tcp_handle) {
    ESP_LOGD(TAG, "📋 Sincronizando discrete inputs RTU → TCP");
    
    esp_err_t result = ESP_OK;
    
    // Sincroniza discrete_reg_params
    result |= modbus_tcp_set_discrete_input(tcp_handle, 0, discrete_reg_params.discrete_input0);
    result |= modbus_tcp_set_discrete_input(tcp_handle, 1, discrete_reg_params.discrete_input1);
    result |= modbus_tcp_set_discrete_input(tcp_handle, 2, discrete_reg_params.discrete_input2);
    result |= modbus_tcp_set_discrete_input(tcp_handle, 3, discrete_reg_params.discrete_input3);
    result |= modbus_tcp_set_discrete_input(tcp_handle, 4, discrete_reg_params.discrete_input4);
    result |= modbus_tcp_set_discrete_input(tcp_handle, 5, discrete_reg_params.discrete_input5);
    result |= modbus_tcp_set_discrete_input(tcp_handle, 6, discrete_reg_params.discrete_input6);
    result |= modbus_tcp_set_discrete_input(tcp_handle, 7, discrete_reg_params.discrete_input7);
    
    if (result == ESP_OK) {
        ESP_LOGD(TAG, "✅ Discrete inputs sincronizados RTU → TCP");
    } else {
        ESP_LOGW(TAG, "⚠️ Alguns discrete inputs falharam na sincronização RTU → TCP");
    }
    
    return result;
}

/* ============================================================================
 * FUNÇÕES DE SINCRONIZAÇÃO: TCP → RTU
 * ============================================================================ */

/**
 * @brief Sincroniza holding registers TCP → RTU
 * 
 * NOTA: Esta função lê os valores da biblioteca TCP e atualiza
 * as variáveis compartilhadas, que são automaticamente refletidas no RTU
 */
static esp_err_t sync_holding_registers_tcp_to_rtu(modbus_tcp_handle_t tcp_handle) {
    ESP_LOGD(TAG, "📋 Sincronizando holding registers TCP → RTU");
    
    esp_err_t result = ESP_OK;
    uint16_t reg_value;
    
    // Sincroniza registradores 0-7 básicos - Agora todos são float
    for (int i = 0; i < 8; i++) {
        if (modbus_tcp_get_holding_register(tcp_handle, i, &reg_value) == ESP_OK) {
            switch (i) {
                case 0: holding_reg_params.holding_data0 = (float)reg_value; break;
                case 1: holding_reg_params.holding_data1 = (float)reg_value; break;
                case 2: holding_reg_params.holding_data2 = (float)reg_value; break;
                case 3: holding_reg_params.holding_data3 = (float)reg_value; break;
                case 4: holding_reg_params.holding_data4 = (float)reg_value; break;
                case 5: holding_reg_params.holding_data5 = (float)reg_value; break;
                case 6: holding_reg_params.holding_data6 = (float)reg_value; break;
                case 7: holding_reg_params.holding_data7 = (float)reg_value; break;
            }
        }
    }
    
    // Sincroniza arrays customizados
    for (int i = 0; i < REG_DATA_SIZE && i < 100; i++) {
        if (modbus_tcp_get_holding_register(tcp_handle, REG_DATA_START + i, &reg_value) == ESP_OK) {
            reg2000[i] = reg_value;
        }
    }
    
    for (int i = 0; i < REG_3000_SIZE && i < 100; i++) {
        if (modbus_tcp_get_holding_register(tcp_handle, REG_3000_START + i, &reg_value) == ESP_OK) {
            reg3000[i] = reg_value;
        }
    }
    
    for (int i = 0; i < REG_4000_SIZE && i < 100; i++) {
        if (modbus_tcp_get_holding_register(tcp_handle, REG_4000_START + i, &reg_value) == ESP_OK) {
            reg4000[i] = reg_value;
        }
    }
    
    ESP_LOGD(TAG, "✅ Holding registers sincronizados TCP → RTU");
    return result;
}

/**
 * @brief Sincroniza coils TCP → RTU
 */
static esp_err_t sync_coils_tcp_to_rtu(modbus_tcp_handle_t tcp_handle) {
    ESP_LOGD(TAG, "📋 Sincronizando coils TCP → RTU");
    
    uint8_t new_coil_port0 = 0;
    uint8_t new_coil_port1 = 0;
    bool coil_value;
    
    // Lê coils individuais e compacta em bytes
    for (int i = 0; i < 8; i++) {
        if (modbus_tcp_get_coil(tcp_handle, i, &coil_value) == ESP_OK && coil_value) {
            new_coil_port0 |= (1 << i);
        }
    }
    
    for (int i = 0; i < 8; i++) {
        if (modbus_tcp_get_coil(tcp_handle, 8 + i, &coil_value) == ESP_OK && coil_value) {
            new_coil_port1 |= (1 << i);
        }
    }
    
    // Atualiza variáveis compartilhadas
    coil_reg_params.coils_port0 = new_coil_port0;
    coil_reg_params.coils_port1 = new_coil_port1;
    
    ESP_LOGD(TAG, "✅ Coils sincronizados TCP → RTU");
    return ESP_OK;
}

/* ============================================================================
 * API PÚBLICA - FUNÇÕES PRINCIPAIS DE SINCRONIZAÇÃO
 * ============================================================================ */

/**
 * @brief Sincroniza todos os registradores RTU → TCP
 */
esp_err_t modbus_sync_all_registers_rtu_to_tcp(modbus_tcp_handle_t tcp_handle) {
    if (tcp_handle == NULL) {
        ESP_LOGE(TAG, "❌ Handle TCP inválido para sincronização");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGD(TAG, "🔄 Iniciando sincronização completa RTU → TCP");
    
    esp_err_t result = ESP_OK;
    
    // Sincroniza todos os tipos de registradores
    result |= sync_holding_registers_rtu_to_tcp(tcp_handle);
    result |= sync_input_registers_rtu_to_tcp(tcp_handle);
    result |= sync_coils_rtu_to_tcp(tcp_handle);
    result |= sync_discrete_inputs_rtu_to_tcp(tcp_handle);
    
    if (result == ESP_OK) {
        ESP_LOGD(TAG, "✅ Sincronização completa RTU → TCP bem sucedida");
    } else {
        ESP_LOGW(TAG, "⚠️ Sincronização RTU → TCP completada com avisos");
    }
    
    return result;
}

/**
 * @brief Sincroniza todos os registradores TCP → RTU
 */
esp_err_t modbus_sync_all_registers_tcp_to_rtu(modbus_tcp_handle_t tcp_handle) {
    if (tcp_handle == NULL) {
        ESP_LOGE(TAG, "❌ Handle TCP inválido para sincronização");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGD(TAG, "🔄 Iniciando sincronização completa TCP → RTU");
    
    esp_err_t result = ESP_OK;
    
    // Sincroniza registradores que podem ser modificados externamente
    result |= sync_holding_registers_tcp_to_rtu(tcp_handle);
    result |= sync_coils_tcp_to_rtu(tcp_handle);
    
    // NOTA: Input registers e discrete inputs normalmente são somente leitura,
    // então não precisam ser sincronizados TCP → RTU
    
    if (result == ESP_OK) {
        ESP_LOGD(TAG, "✅ Sincronização completa TCP → RTU bem sucedida");
    } else {
        ESP_LOGW(TAG, "⚠️ Sincronização TCP → RTU completada com avisos");
    }
    
    return result;
}

/**
 * @brief Sincronização bidirecional completa
 * 
 * Esta função executa sincronização nos dois sentidos e é útil
 * durante transições de modo ou inicialização
 */
esp_err_t modbus_sync_bidirectional(modbus_tcp_handle_t tcp_handle, bool rtu_is_master) {
    ESP_LOGI(TAG, "🔄 Iniciando sincronização bidirecional (RTU master: %s)", 
             rtu_is_master ? "sim" : "não");
    
    esp_err_t result = ESP_OK;
    
    if (rtu_is_master) {
        // RTU é a fonte da verdade - sincroniza RTU → TCP
        result = modbus_sync_all_registers_rtu_to_tcp(tcp_handle);
    } else {
        // TCP é a fonte da verdade - sincroniza TCP → RTU  
        result = modbus_sync_all_registers_tcp_to_rtu(tcp_handle);
    }
    
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "✅ Sincronização bidirecional concluída");
    } else {
        ESP_LOGW(TAG, "⚠️ Sincronização bidirecional completada com avisos");
    }
    
    return result;
}

/**
 * @brief Força sincronização de registradores críticos apenas
 * 
 * Versão otimizada que sincroniza apenas registradores mais importantes,
 * útil para chamadas frequentes
 */
esp_err_t modbus_sync_critical_registers_only(modbus_tcp_handle_t tcp_handle, bool rtu_to_tcp) {
    ESP_LOGD(TAG, "🔄 Sincronizando apenas registradores críticos (%s)", 
             rtu_to_tcp ? "RTU→TCP" : "TCP→RTU");
    
    esp_err_t result = ESP_OK;
    
    if (rtu_to_tcp) {
        // Sincroniza apenas reg2000 (dados principais) e alguns holding registers
        for (int i = 0; i < 10 && i < REG_DATA_SIZE; i++) {
            result |= modbus_tcp_set_holding_register(tcp_handle, REG_DATA_START + i, reg2000[i]);
        }
    } else {
        // Sincroniza apenas registradores críticos TCP → RTU
        uint16_t reg_value;
        for (int i = 0; i < 10 && i < REG_DATA_SIZE; i++) {
            if (modbus_tcp_get_holding_register(tcp_handle, REG_DATA_START + i, &reg_value) == ESP_OK) {
                reg2000[i] = reg_value;
            }
        }
    }
    
    return result;
}