#ifndef AP_MANAGER_CONFIG_H
#define AP_MANAGER_CONFIG_H

#include "ap_manager.h"
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Módulo de configuração persistente para AP Manager
 * 
 * Gerencia salvamento e carregamento de configurações em:
 * - SPIFFS (prioridade primária)
 * - NVS (fallback/backup)
 */

// Caminhos dos arquivos de configuração
#define AP_CONFIG_FILE_PATH "/spiffs/data/config/ap_config.json"
#define STA_CONFIG_FILE_PATH "/spiffs/data/config/sta_config.json"

// Chaves NVS para backup
#define AP_CONFIG_NVS_NAMESPACE "ap_manager"
#define AP_CONFIG_NVS_KEY "ap_config"
#define STA_CONFIG_NVS_KEY "sta_config"

/**
 * @brief Inicializa sistema de configuração (SPIFFS e NVS)
 * 
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t ap_manager_config_init(void);

/**
 * @brief Salva configuração AP em SPIFFS e NVS
 * 
 * @param config Configuração do AP
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t ap_manager_config_save_ap(const ap_manager_config_t *config);

/**
 * @brief Carrega configuração AP (SPIFFS prioritário, NVS fallback)
 * 
 * @param config Buffer para receber configuração
 * @return esp_err_t ESP_OK se sucesso, ESP_ERR_NOT_FOUND se não encontrado
 */
esp_err_t ap_manager_config_load_ap(ap_manager_config_t *config);

/**
 * @brief Salva configuração Station em SPIFFS e NVS
 * 
 * @param config Configuração Station
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t ap_manager_config_save_sta(const ap_manager_sta_config_t *config);

/**
 * @brief Carrega configuração Station (SPIFFS prioritário, NVS fallback)
 * 
 * @param config Buffer para receber configuração
 * @return esp_err_t ESP_OK se sucesso, ESP_ERR_NOT_FOUND se não encontrado
 */
esp_err_t ap_manager_config_load_sta(ap_manager_sta_config_t *config);

/**
 * @brief Remove todas as configurações salvas
 * 
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t ap_manager_config_erase_all(void);

/**
 * @brief Verifica se existe configuração AP salva
 * 
 * @return true se existe configuração salva
 */
bool ap_manager_config_ap_exists(void);

/**
 * @brief Verifica se existe configuração Station salva
 * 
 * @return true se existe configuração salva
 */
bool ap_manager_config_sta_exists(void);

/**
 * @brief Exporta configurações para JSON string
 * 
 * @param json_out Ponteiro para receber string JSON (deve ser liberado com free())
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t ap_manager_config_export_json(char **json_out);

/**
 * @brief Importa configurações de JSON string
 * 
 * @param json_str String JSON com configurações
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t ap_manager_config_import_json(const char *json_str);

#ifdef __cplusplus
}
#endif

#endif // AP_MANAGER_CONFIG_H