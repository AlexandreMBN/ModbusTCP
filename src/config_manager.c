// Restored and extended configuration manager
#include "config_manager.h"
#include "esp_spiffs.h"
#include "cJSON.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <nvs_flash.h>
#include <nvs.h>

static const char *TAG = "CONFIG";

// Inicializa SPIFFS (uma vez)
static void init_spiffs(void) {
    static bool mounted = false;
    if (mounted) return;

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));
    mounted = true;
}

// Helper to extract simple string from a tiny JSON blob
static void extract_value(const char* json, const char* key, char* dest, size_t dest_len) {
    if (!json || !key || !dest || dest_len == 0) return;
    char search_key[32];
    snprintf(search_key, sizeof(search_key), "\"%s\":\"", key);
    char* ptr = strstr(json, search_key);
    if (ptr) {
        ptr += strlen(search_key);
        char* end = strchr(ptr, '"');
        if (end) {
            size_t len = (size_t)(end - ptr);
            if (len >= dest_len) len = dest_len - 1;
            strncpy(dest, ptr, len);
            dest[len] = '\0';
            return;
        }
    }
    dest[0] = '\0';
}

esp_err_t save_config(void) {
    init_spiffs();

    // monta JSON a partir de registradores de configuração. 
    //---- Faixa 1000 ----
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "baudrate", holding_reg1000_params.reg1000[baudrate]);
    cJSON_AddNumberToObject(root, "endereco", holding_reg1000_params.reg1000[endereco]);
    cJSON_AddNumberToObject(root, "paridade", holding_reg1000_params.reg1000[paridade]);

    // ----- Faixa 4000 ----
    cJSON *reg4000_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(reg4000_obj, "lambdaValue", reg4000[lambdaValue]);
    cJSON_AddNumberToObject(reg4000_obj, "lambdaRef",   reg4000[lambdaRef]);
    cJSON_AddNumberToObject(reg4000_obj, "heatValue",   reg4000[heatValue]);
    cJSON_AddNumberToObject(reg4000_obj, "heatRef",     reg4000[heatRef]);
    cJSON_AddNumberToObject(reg4000_obj, "output_mb",   reg4000[output_mb]);
    cJSON_AddNumberToObject(reg4000_obj, "PROBE_DEMAGED",   reg4000[PROBE_DEMAGED]);
    cJSON_AddNumberToObject(reg4000_obj, "PROBE_TEMP_OUT_OF_RANGE",   reg4000[PROBE_TEMP_OUT_OF_RANGE]);
    cJSON_AddNumberToObject(reg4000_obj, "COMPRESSOR_FAIL",   reg4000[COMPRESSOR_FAIL]);
    cJSON_AddItemToObject(root, "reg4000", reg4000_obj);

    // ---- Faixa 6000 ----
    cJSON *reg6000_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(reg6000_obj, "maxDac0", reg6000[maxDac0]);
    cJSON_AddNumberToObject(reg6000_obj, "forcaValorDAC", reg6000[forcaValorDAC]);
    cJSON_AddNumberToObject(reg6000_obj, "nada", reg6000[nada]);
    cJSON_AddNumberToObject(reg6000_obj, "dACGain0", reg6000[dACGain0]);
    cJSON_AddNumberToObject(reg6000_obj, "dACOffset0", reg6000[dACOffset0]);
    cJSON_AddItemToObject(root, "reg6000", reg6000_obj);

    // ---- Faixa 9000 ----
    cJSON *reg9000_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(reg9000_obj, "valorZero",     reg9000[valorZero]);
    cJSON_AddNumberToObject(reg9000_obj, "valorUm",     reg9000[valorUm]);
    cJSON_AddNumberToObject(reg9000_obj, "firmVerHi",     reg9000[firmVerHi]);
    cJSON_AddNumberToObject(reg9000_obj, "firmVerLo",  reg9000[firmVerLo]);
    cJSON_AddNumberToObject(reg9000_obj, "valorQuatro",reg9000[valorQuatro]);
    cJSON_AddNumberToObject(reg9000_obj, "valorCinco",  reg9000[valorCinco]);
    cJSON_AddNumberToObject(reg9000_obj, "lotnum0",  reg9000[lotnum0]);
    cJSON_AddNumberToObject(reg9000_obj, "lotnum1",  reg9000[lotnum1]);
    cJSON_AddNumberToObject(reg9000_obj, "lotnum2",  reg9000[lotnum2]);
    cJSON_AddNumberToObject(reg9000_obj, "lotnum3",  reg9000[lotnum3]);
    cJSON_AddNumberToObject(reg9000_obj, "lotnum4",  reg9000[lotnum4]);
    cJSON_AddNumberToObject(reg9000_obj, "lotnum5",  reg9000[lotnum5]);
    cJSON_AddNumberToObject(reg9000_obj, "wafnum",  reg9000[wafnum]);
    cJSON_AddNumberToObject(reg9000_obj, "coordx0",  reg9000[coordx0]);
    cJSON_AddNumberToObject(reg9000_obj, "coordx1",  reg9000[coordx1]);
    cJSON_AddNumberToObject(reg9000_obj, "valor17",  reg9000[valor17]);
    cJSON_AddNumberToObject(reg9000_obj, "valor18",  reg9000[valor18]);
    cJSON_AddNumberToObject(reg9000_obj, "valor19",  reg9000[valor19]);
    cJSON_AddItemToObject(root, "reg9000", reg9000_obj);

    // ---- Grava no arquivo ----
    char *json_str = cJSON_Print(root);

    FILE *f = fopen("/spiffs/config.json", "w");
    if (!f) {
        ESP_LOGE(TAG, "Erro ao abrir config.json para escrita");
        cJSON_Delete(root);
        free(json_str);
        return ESP_FAIL;
    }

    fprintf(f, "%s", json_str);
    ESP_LOGI(TAG, "json_str: %s", json_str);
    fclose(f);

    ESP_LOGI(TAG, "Configuração salva!");
    cJSON_Delete(root);
    free(json_str);
    return ESP_OK;
}

esp_err_t load_config(void) {
    init_spiffs();

    FILE *f = fopen("/spiffs/config.json", "r");
    if (!f) {
        ESP_LOGW(TAG, "config.json não encontrado, mantendo valores padrão");
        return ESP_FAIL;
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
        ESP_LOGE(TAG, "Erro ao parsear config.json");
        free(data);
        return ESP_FAIL;
    }

    // ---- Faixa 1000 ----
    cJSON *item = cJSON_GetObjectItem(root, "baudrate");
    if (item && cJSON_IsNumber(item)) holding_reg1000_params.reg1000[baudrate] = item->valueint;
    item = cJSON_GetObjectItem(root, "endereco");
    if (item && cJSON_IsNumber(item)) holding_reg1000_params.reg1000[endereco] = item->valueint;
    item = cJSON_GetObjectItem(root, "paridade");
    if (item && cJSON_IsNumber(item)) holding_reg1000_params.reg1000[paridade] = item->valueint;

    // ---- Faixa 4000 ----
    cJSON *reg4000_obj = cJSON_GetObjectItem(root, "reg4000");
    if (reg4000_obj) {
        item = cJSON_GetObjectItem(reg4000_obj, "lambdaValue"); if (item && cJSON_IsNumber(item)) reg4000[lambdaValue] = item->valueint;
        item = cJSON_GetObjectItem(reg4000_obj, "lambdaRef");   if (item && cJSON_IsNumber(item)) reg4000[lambdaRef]   = item->valueint;
        item = cJSON_GetObjectItem(reg4000_obj, "heatValue");   if (item && cJSON_IsNumber(item)) reg4000[heatValue]   = item->valueint;
        item = cJSON_GetObjectItem(reg4000_obj, "heatRef");     if (item && cJSON_IsNumber(item)) reg4000[heatRef]     = item->valueint;
        item = cJSON_GetObjectItem(reg4000_obj, "output_mb");   if (item && cJSON_IsNumber(item)) reg4000[output_mb]   = item->valueint;
    }

     // ---- Faixa 6000 ----
    cJSON *reg6000_obj = cJSON_GetObjectItem(root, "reg6000");
    if (reg6000_obj) {
        item = cJSON_GetObjectItem(reg6000_obj, "maxDac0"); if (item && cJSON_IsNumber(item)) reg6000[maxDac0] = item->valueint;
        item = cJSON_GetObjectItem(reg6000_obj, "forcaValorDAC"); if (item && cJSON_IsNumber(item)) reg6000[forcaValorDAC] = item->valueint;
        item = cJSON_GetObjectItem(reg6000_obj, "nada"); if (item && cJSON_IsNumber(item)) reg6000[nada] = item->valueint;
        item = cJSON_GetObjectItem(reg6000_obj, "dACGain0"); if (item && cJSON_IsNumber(item)) reg6000[dACGain0] = item->valueint;
        item = cJSON_GetObjectItem(reg6000_obj, "dACOffset0"); if (item && cJSON_IsNumber(item)) reg6000[dACOffset0] = item->valueint;
    }

     // ---- Faixa 9000 ----
    cJSON *reg9000_obj = cJSON_GetObjectItem(root, "reg9000");
    if (reg9000_obj) {
        item = cJSON_GetObjectItem(reg9000_obj, "valorZero"); if (item && cJSON_IsNumber(item)) reg9000[valorZero] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "valorUm"); if (item && cJSON_IsNumber(item)) reg9000[valorUm] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "firmVerHi"); if (item && cJSON_IsNumber(item)) reg9000[firmVerHi] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "firmVerLo"); if (item && cJSON_IsNumber(item)) reg9000[firmVerLo] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "valorQuatro"); if (item && cJSON_IsNumber(item)) reg9000[valorQuatro] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "valorCinco"); if (item && cJSON_IsNumber(item)) reg9000[valorCinco] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "lotnum0"); if (item && cJSON_IsNumber(item)) reg9000[lotnum0] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "lotnum1"); if (item && cJSON_IsNumber(item)) reg9000[lotnum1] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "lotnum2"); if (item && cJSON_IsNumber(item)) reg9000[lotnum2] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "lotnum3"); if (item && cJSON_IsNumber(item)) reg9000[lotnum3] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "lotnum4"); if (item && cJSON_IsNumber(item)) reg9000[lotnum4] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "lotnum5"); if (item && cJSON_IsNumber(item)) reg9000[lotnum5] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "wafnum"); if (item && cJSON_IsNumber(item)) reg9000[wafnum] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "coordx0"); if (item && cJSON_IsNumber(item)) reg9000[coordx0] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "coordx1"); if (item && cJSON_IsNumber(item)) reg9000[coordx1] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "valor17"); if (item && cJSON_IsNumber(item)) reg9000[valor17] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "valor18"); if (item && cJSON_IsNumber(item)) reg9000[valor18] = item->valueint;
        item = cJSON_GetObjectItem(reg9000_obj, "valor19"); if (item && cJSON_IsNumber(item)) reg9000[valor19] = item->valueint;
    }

    ESP_LOGI(TAG, "Config carregada: baud=%d, endereco=%d, paridade=%d",
             holding_reg1000_params.reg1000[baudrate],
             holding_reg1000_params.reg1000[endereco],
             holding_reg1000_params.reg1000[paridade]);

     ESP_LOGI(TAG, "reg4000: lambdaValue=%d, lambdaRef=%d, heatValue=%d, heatRef=%d, output_mb=%d",
             reg4000[lambdaValue], reg4000[lambdaRef],
             reg4000[heatValue], reg4000[heatRef],
             reg4000[output_mb]);

    ESP_LOGI(TAG, "reg6000: maxDac0=%d, forcaValorDAC=%d, nada=%d, dACGain0=%d, dACOffset0=%d",
             reg6000[maxDac0], reg6000[forcaValorDAC], reg6000[nada], reg6000[dACGain0], reg6000[dACOffset0]);

    ESP_LOGI(TAG, "reg9000: valorZero=%d, valorUm=%d, firmVerHi=%d, firmVerLo=%d, valorQuatro=%d, valorCinco=%d, lotnum0=%d, lotnum1=%d, lotnum2=%d, lotnum3=%d, lotnum4=%d, lotnum5=%d, wafnum=%d, coordx0=%d, coordx1=%d, valor17=%d, valor18=%d, valor19=%d",
             reg9000[valorZero], reg9000[valorUm], reg9000[firmVerHi], reg9000[firmVerLo], reg9000[valorQuatro], reg9000[valorCinco], reg9000[lotnum0], reg9000[lotnum1], reg9000[lotnum2], reg9000[lotnum3], reg9000[lotnum4], reg9000[lotnum5], reg9000[wafnum], reg9000[coordx0], reg9000[coordx1], reg9000[valor17], reg9000[valor18], reg9000[valor19]);

    cJSON_Delete(root);
    free(data);
    return ESP_OK;
}

// ================= Login state (NVS) =================
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

// ================= WiFi credentials (/spiffs/config.json) =================
void save_wifi_config(const char* ssid, const char* password) {
    // Salvar WiFi credentials no NVS (namespace wifi_config)
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao abrir NVS wifi_config: %s", esp_err_to_name(err));
        return;
    }
    
    // Salvar SSID e senha
    if (ssid && strlen(ssid) > 0) {
        nvs_set_str(nvs_handle, "wifi_ssid", ssid);
    }
    if (password) {
        nvs_set_str(nvs_handle, "wifi_password", password);
    }
    
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao salvar WiFi config no NVS: %s", esp_err_to_name(err));
    } else {
        // Log details of saved credentials clearly (SSID and password)
        ESP_LOGI(TAG, "WiFi config salvo com sucesso no NVS - SSID: '%s' Password: '%s'", 
                 ssid ? ssid : "", password ? password : "");
    }
}

void read_wifi_config(char* ssid, size_t ssid_len, char* password, size_t password_len) {
    // Inicializar strings vazias
    if (ssid && ssid_len) ssid[0] = '\0';
    if (password && password_len) password[0] = '\0';
    
    // Ler WiFi credentials do NVS (namespace wifi_config)
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Erro ao abrir NVS wifi_config para leitura: %s", esp_err_to_name(err));
        return;
    }
    
    // Ler SSID
    if (ssid && ssid_len) {
        size_t required_size = ssid_len;
        err = nvs_get_str(nvs_handle, "wifi_ssid", ssid, &required_size);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Erro ao ler wifi_ssid do NVS: %s", esp_err_to_name(err));
            ssid[0] = '\0';
        }
    }
    
    // Ler senha
    if (password && password_len) {
        size_t required_size = password_len;
        err = nvs_get_str(nvs_handle, "wifi_password", password, &required_size);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Erro ao ler wifi_password do NVS: %s", esp_err_to_name(err));
            password[0] = '\0';
        }
    }
    
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "WiFi config lido do NVS - SSID: %s", ssid ? ssid : "NULL");
}

// ================= Network config (/spiffs/network_config.json) =================
void save_network_config(const char* ip, const char* mask, const char* gateway, const char* dns) {
    init_spiffs();
    const char* path = "/spiffs/network_config.json";
    FILE* f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Erro ao abrir %s para escrita!", path);
        return;
    }
    int ret = fprintf(f, "{\"ip\":\"%s\",\"mask\":\"%s\",\"gateway\":\"%s\",\"dns\":\"%s\"}",
                     ip ? ip : "", mask ? mask : "", gateway ? gateway : "", dns ? dns : "");
    fclose(f);
    if (ret < 0) {
        ESP_LOGE(TAG, "Erro ao escrever no arquivo %s", path);
    } else {
        ESP_LOGI(TAG, "%s salvo com sucesso!", path);
    }
}

void read_network_config(char* ip, size_t ip_len, char* mask, size_t mask_len,
                        char* gateway, size_t gateway_len, char* dns, size_t dns_len) {
    init_spiffs();
    if (ip && ip_len) ip[0] = '\0';
    if (mask && mask_len) mask[0] = '\0';
    if (gateway && gateway_len) gateway[0] = '\0';
    if (dns && dns_len) dns[0] = '\0';
    const char* path = "/spiffs/network_config.json";
    FILE* f = fopen(path, "r");
    if (!f) {
        ESP_LOGW(TAG, "Arquivo %s não encontrado!", path);
        return;
    }
    char buf[512] = {0};
    size_t n = fread(buf, 1, sizeof(buf)-1, f);
    buf[n] = '\0';
    fclose(f);
    extract_value(buf, "ip", ip, ip_len);
    extract_value(buf, "mask", mask, mask_len);
    extract_value(buf, "gateway", gateway, gateway_len);
    extract_value(buf, "dns", dns, dns_len);
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
