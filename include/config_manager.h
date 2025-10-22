#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "modbus_params.h"
#include "esp_err.h"
#include <stddef.h>  // for size_t
#include <stdbool.h>

// Salva toda a configuração em JSON
esp_err_t save_config(void);

// Carrega a configuração de JSON
esp_err_t load_config(void);

// ================= Additional persistence helpers (migrated from storage.h/.c) =================

// Login state in NVS
void save_login_state(bool state);
bool load_login_state(void);
// Root login state in NVS
void save_login_state_root(bool state);
bool load_login_state_root(void);

// WiFi credentials in /spiffs/config.json
void save_wifi_config(const char* ssid, const char* password);
void read_wifi_config(char* ssid, size_t ssid_len, char* password, size_t password_len);

// Network config in /spiffs/network_config.json
void save_network_config(const char* ip, const char* mask, const char* gateway, const char* dns);
void read_network_config(char* ip, size_t ip_len, char* mask, size_t mask_len,
                        char* gateway, size_t gateway_len, char* dns, size_t dns_len);

// Níveis de usuário para controle de acesso
typedef enum {
    USER_LEVEL_NONE = 0,        // Não logado
    USER_LEVEL_BASIC = 1,       // Usuário padrão (adm)
    USER_LEVEL_ADMIN = 2        // Administrador (root) - acesso completo
} user_level_t;

// Funções para controle de acesso
void save_user_level(user_level_t level);
user_level_t load_user_level(void);
bool check_access_permission(user_level_t required_level);
bool can_modify_register_range(int register_base);

#endif // CONFIG_MANAGER_H
