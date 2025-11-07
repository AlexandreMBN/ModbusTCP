/**
 * @file modbus_register_sync.h  
 * @brief Interface para sincronização de registradores entre RTU e TCP
 * 
 * Este header define as funções públicas para sincronização bidirecional
 * de registradores Modbus entre implementações RTU e TCP.
 * 
 * @author Sistema ESP32
 * @date 2025
 */

#ifndef MODBUS_REGISTER_SYNC_H
#define MODBUS_REGISTER_SYNC_H

#include "esp_err.h"
#include "modbus_tcp_slave.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * FUNÇÕES PÚBLICAS DE SINCRONIZAÇÃO
 * ============================================================================ */

/**
 * @brief Sincroniza todos os registradores RTU → TCP
 * 
 * Copia valores dos registradores compartilhados (RTU) para a biblioteca TCP
 * 
 * @param tcp_handle Handle da instância TCP ativa
 * @return ESP_OK em sucesso, código de erro em falha
 */
esp_err_t modbus_sync_all_registers_rtu_to_tcp(modbus_tcp_handle_t tcp_handle);

/**
 * @brief Sincroniza todos os registradores TCP → RTU
 * 
 * Lê valores da biblioteca TCP e atualiza registradores compartilhados (RTU)
 * 
 * @param tcp_handle Handle da instância TCP ativa  
 * @return ESP_OK em sucesso, código de erro em falha
 */
esp_err_t modbus_sync_all_registers_tcp_to_rtu(modbus_tcp_handle_t tcp_handle);

/**
 * @brief Sincronização bidirecional completa
 * 
 * @param tcp_handle Handle da instância TCP
 * @param rtu_is_master Se true, RTU é fonte da verdade (RTU→TCP), senão TCP→RTU
 * @return ESP_OK em sucesso
 */
esp_err_t modbus_sync_bidirectional(modbus_tcp_handle_t tcp_handle, bool rtu_is_master);

/**
 * @brief Sincroniza apenas registradores críticos (otimizada)
 * 
 * @param tcp_handle Handle da instância TCP
 * @param rtu_to_tcp Direção da sincronização
 * @return ESP_OK em sucesso
 */
esp_err_t modbus_sync_critical_registers_only(modbus_tcp_handle_t tcp_handle, bool rtu_to_tcp);

#ifdef __cplusplus
}
#endif

#endif // MODBUS_REGISTER_SYNC_H