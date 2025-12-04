#ifndef AP_MANAGER_H
#define AP_MANAGER_H

#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_err.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Biblioteca AP Manager para ESP32
 * 
 * Esta biblioteca oferece funcionalidades completas para gerenciamento
 * de Access Point WiFi no ESP32, incluindo:
 * - Configuração dinâmica de SSID, password e IP
 * - Salvamento persistente em NVS e SPIFFS
 * - Scan de redes WiFi disponíveis
 * - Conexão automática em modo Station
 * - Gerenciamento de eventos WiFi
 * - Thread-safe operations
 */

// Constantes
#define AP_MANAGER_MAX_APs 20
#define AP_MANAGER_SSID_MAX_LEN 32
#define AP_MANAGER_PASS_MAX_LEN 64
#define AP_MANAGER_IP_MAX_LEN 16
#define AP_MANAGER_STATUS_MSG_MAX_LEN 256

// Estruturas de configuração
typedef struct {
    char ssid[AP_MANAGER_SSID_MAX_LEN];
    char password[AP_MANAGER_PASS_MAX_LEN];
    char ip[AP_MANAGER_IP_MAX_LEN];
    uint8_t channel;
    uint8_t max_connections;
    bool hidden;
} ap_manager_config_t;

typedef struct {
    char ssid[AP_MANAGER_SSID_MAX_LEN];
    char password[AP_MANAGER_PASS_MAX_LEN];
} ap_manager_sta_config_t;

typedef struct {
    bool is_connected;
    bool ap_active;
    char current_ssid[AP_MANAGER_SSID_MAX_LEN];
    char ip_address[AP_MANAGER_IP_MAX_LEN];
    int rssi;
    char status_message[AP_MANAGER_STATUS_MSG_MAX_LEN];
} ap_manager_status_t;

// Configurações padrão
#define AP_MANAGER_DEFAULT_SSID "ESP32-MCT-01"
#define AP_MANAGER_DEFAULT_PASSWORD "12345678"
#define AP_MANAGER_DEFAULT_IP "192.168.4.1"
#define AP_MANAGER_DEFAULT_CHANNEL 1
#define AP_MANAGER_DEFAULT_MAX_CONN 4

// Eventos personalizados
ESP_EVENT_DECLARE_BASE(AP_MANAGER_EVENTS);

typedef enum {
    AP_MANAGER_EVENT_AP_STARTED,
    AP_MANAGER_EVENT_AP_STOPPED,
    AP_MANAGER_EVENT_STA_CONNECTED,
    AP_MANAGER_EVENT_STA_DISCONNECTED,
    AP_MANAGER_EVENT_STA_GOT_IP,
    AP_MANAGER_EVENT_SCAN_COMPLETED,
    AP_MANAGER_EVENT_CONFIG_SAVED,
    AP_MANAGER_EVENT_CONFIG_LOADED
} ap_manager_event_id_t;

// Callback para eventos customizados
typedef void (*ap_manager_event_cb_t)(ap_manager_event_id_t event, void *event_data);

/**
 * @brief Inicializa o sistema AP Manager
 * 
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t ap_manager_init(void);

/**
 * @brief Deinicializa e limpa recursos do AP Manager
 * 
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t ap_manager_deinit(void);

/**
 * @brief Inicia o Access Point com configurações salvas ou padrão
 * 
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t ap_manager_start_ap(void);

/**
 * @brief Para o Access Point
 * 
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t ap_manager_stop_ap(void);

/**
 * @brief Configura e salva configurações do Access Point
 * 
 * @param config Configurações do AP
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t ap_manager_set_ap_config(const ap_manager_config_t *config);

/**
 * @brief Carrega configurações do Access Point
 * 
 * @param config Buffer para receber configurações
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t ap_manager_get_ap_config(ap_manager_config_t *config);

/**
 * @brief Conecta a uma rede WiFi como Station
 * 
 * @param ssid Nome da rede
 * @param password Senha da rede
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t ap_manager_connect_sta(const char *ssid, const char *password);

/**
 * @brief Desconecta do modo Station
 * 
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t ap_manager_disconnect_sta(void);

/**
 * @brief Salva configurações Station para reconexão automática
 * 
 * @param config Configurações Station
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t ap_manager_set_sta_config(const ap_manager_sta_config_t *config);

/**
 * @brief Carrega configurações Station salvas
 * 
 * @param config Buffer para receber configurações
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t ap_manager_get_sta_config(ap_manager_sta_config_t *config);

/**
 * @brief Inicia scan de redes WiFi disponíveis
 * 
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t ap_manager_start_scan(void);

/**
 * @brief Obtém lista de redes WiFi encontradas no último scan
 * 
 * @param records Array para receber registros
 * @param max_records Tamanho máximo do array
 * @param count Ponteiro para receber número de redes encontradas
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t ap_manager_get_scan_results(wifi_ap_record_t *records, uint16_t max_records, uint16_t *count);

/**
 * @brief Verifica se há scan em progresso
 * 
 * @return true se scan em progresso
 */
bool ap_manager_is_scan_in_progress(void);

/**
 * @brief Obtém status atual do WiFi
 * 
 * @return ap_manager_status_t Estrutura com status
 */
ap_manager_status_t ap_manager_get_status(void);

/**
 * @brief Verifica se AP Manager está inicializado
 * 
 * @return true se inicializado
 */
bool ap_manager_is_initialized(void);

/**
 * @brief Define mensagem de status personalizada
 * 
 * @param message Mensagem de status
 */
void ap_manager_set_status_message(const char *message);

/**
 * @brief Registra callback para eventos
 * 
 * @param callback Função callback
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t ap_manager_set_event_callback(ap_manager_event_cb_t callback);

/**
 * @brief Remove callback de eventos
 * 
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t ap_manager_unset_event_callback(void);

/**
 * @brief Aplica configuração de IP estático para o modo Station
 * 
 * @param ip Endereço IP
 * @param netmask Máscara de rede
 * @param gateway Gateway padrão
 * @param dns Servidor DNS
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t ap_manager_set_static_ip(const char *ip, const char *netmask, const char *gateway, const char *dns);

/**
 * @brief Habilita DHCP para o modo Station
 * 
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t ap_manager_enable_dhcp(void);

/**
 * @brief Troca automaticamente para modo Station-only após conexão bem-sucedida
 * 
 * @param timeout_ms Timeout em milissegundos para aguardar conexão
 * @return esp_err_t ESP_OK se task foi criada com sucesso
 */
esp_err_t ap_manager_auto_switch_to_sta(uint32_t timeout_ms);

/**
 * @brief Força o modo WiFi (AP, STA ou APSTA)
 * 
 * @param mode Modo WiFi desejado
 * @return esp_err_t ESP_OK se sucesso
 */
esp_err_t ap_manager_set_wifi_mode(wifi_mode_t mode);

/**
 * @brief Obtém configurações padrão para AP
 * 
 * @param config Buffer para receber configurações padrão
 */
void ap_manager_get_default_ap_config(ap_manager_config_t *config);

#ifdef __cplusplus
}
#endif

#endif // AP_MANAGER_H