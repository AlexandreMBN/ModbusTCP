#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <esp_wifi.h>
#include <stdbool.h>
#include <esp_err.h>   // <-- adicionei para usar esp_err_t

#define MAX_APs 20
#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASS_MAX_LEN 64
#define WIFI_STATUS_MSG_MAX_LEN 256

// Estrutura para status WiFi
typedef struct {
    bool is_connected;
    bool ap_active;
    char current_ssid[WIFI_SSID_MAX_LEN];
    char ip_address[16];
    int rssi;
    char status_message[WIFI_STATUS_MSG_MAX_LEN];
} wifi_status_t;

extern wifi_ap_record_t ap_records[MAX_APs];
extern uint16_t ap_count;

// Funções principais
esp_err_t wifi_scan(void);   // <-- aqui alterei de void para esp_err_t
void start_wifi_ap(void);
void wifi_connect(const char* ssid, const char* password);

// Novas funções de gestão
void wifi_cleanup(void);
void wifi_disconnect(void);
wifi_status_t wifi_get_status(void);
bool wifi_is_initialized(void);
void wifi_set_status_message(const char* message);

// Funções de configuração
esp_err_t wifi_set_ap_config(const char* ssid, const char* password, uint8_t channel);
esp_err_t wifi_set_sta_config(const char* ssid, const char* password);

// Funções para configuração de rede estática
esp_err_t wifi_apply_static_ip(const char* ip, const char* netmask, const char* gateway, const char* dns);
esp_err_t wifi_apply_dhcp(void);

// Returns a consistent snapshot of the last scan results. out_count will be set to
// the number of entries copied (may be 0). Caller should provide an array with
// capacity for MAX_APs entries.
void wifi_get_ap_list_snapshot(wifi_ap_record_t *out_records, uint16_t *out_count);

// Start an asynchronous scan task (returns ESP_OK if task created)
esp_err_t wifi_start_scan_async(void);

// Query scan status
bool wifi_is_scan_in_progress(void);
// Returns estimated milliseconds left for the current scan (best-effort)
uint32_t wifi_scan_time_left_ms(void);
// Returns last scan duration in ms (0 if none)
uint32_t wifi_last_scan_duration_ms(void);

// After initiating a connect, wait up to timeout_ms for STA to become connected;
// if connected, switch device to STA-only mode (disable AP). Returns ESP_OK if
// the task to monitor was created, or error if the task couldn't be started.
esp_err_t wifi_switch_to_sta_on_successful_connect(uint32_t timeout_ms);

#endif // WIFI_MANAGER_H
