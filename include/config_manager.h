#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "modbus_params.h"
#include "esp_err.h"
#include <stddef.h>  // for size_t
#include <stdbool.h>

// ================= Nova Estrutura de Arquivos =================
// /spiffs/rtu_config.json → Configurações Modbus RTU e registradores
// /spiffs/ap_config.json → Configurações WiFi Access Point
// /spiffs/sta_config.json → Configurações WiFi Station
// /spiffs/mqtt_config.json → Configurações cliente MQTT
// /spiffs/network_config.json → Configurações de rede (IP, gateway, DNS)

// ================= RTU Config (/spiffs/rtu_config.json) =================
esp_err_t save_rtu_config(void);
esp_err_t load_rtu_config(void);

// ================= AP Config (/spiffs/ap_config.json) =================
typedef struct {
    char ssid[32];
    char username[32];
    char password[64];
    char ip[16];
} ap_config_t;

esp_err_t save_ap_config(const ap_config_t* config);
esp_err_t load_ap_config(ap_config_t* config);

// ================= STA Config (/spiffs/sta_config.json) =================
typedef struct {
    char ssid[32];
    char password[64];
} sta_config_t;

esp_err_t save_sta_config(const sta_config_t* config);
esp_err_t load_sta_config(sta_config_t* config);

// ================= MQTT Config (/spiffs/mqtt_config.json) =================
typedef struct {
    char broker_url[128];
    char client_id[32];
    char username[32];
    char password[64];
    uint16_t port;
    uint8_t qos;
    bool retain;
    bool tls_enabled;
    char ca_path[128];
    bool enabled;
    uint32_t publish_interval_ms;
} mqtt_config_t;

esp_err_t save_mqtt_config(const mqtt_config_t* config);
esp_err_t load_mqtt_config(mqtt_config_t* config);

// ================= Network Config (/spiffs/network_config.json) =================
typedef struct {
    char ip[16];
    char mask[16];
    char gateway[16];
    char dns[16];
} network_config_t;

esp_err_t save_network_config(const network_config_t* config);
esp_err_t load_network_config(network_config_t* config);

// ================= Login state (mantido no NVS por segurança) =================
void save_login_state(bool state);
bool load_login_state(void);
void save_login_state_root(bool state);
bool load_login_state_root(void);

// ================= User Level Control (mantido no NVS) =================
typedef enum {
    USER_LEVEL_NONE = 0,        // Não logado
    USER_LEVEL_BASIC = 1,       // Usuário padrão (adm)
    USER_LEVEL_ADMIN = 2        // Administrador (root) - acesso completo
} user_level_t;

void save_user_level(user_level_t level);
user_level_t load_user_level(void);
bool check_access_permission(user_level_t required_level);
bool can_modify_register_range(int register_base);

// ================= Helper Functions =================
void ensure_data_config_dir(void);

// ================= Legacy Functions (para compatibilidade) =================
// Mantidas para compatibilidade com código existente
esp_err_t save_config(void);  // Chama save_rtu_config()
esp_err_t load_config(void);  // Chama load_rtu_config()

// Wrappers para WiFi (migração gradual)
void save_wifi_config(const char* ssid, const char* password);
void read_wifi_config(char* ssid, size_t ssid_len, char* password, size_t password_len);

#endif // CONFIG_MANAGER_H
