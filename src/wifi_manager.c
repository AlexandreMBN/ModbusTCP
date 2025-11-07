#include "wifi_manager.h"
#include "webserver.h"
#include "config_manager.h"
#include <string.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <stdio.h>
#include <nvs_flash.h>
#include <nvs.h>

static const char *TAG = "WIFI_MANAGER";

wifi_ap_record_t ap_records[MAX_APs];
uint16_t ap_count = 0;

static TaskHandle_t fallback_task_handle = NULL;
static TaskHandle_t ap_disable_task_handle = NULL;
static SemaphoreHandle_t wifi_mutex = NULL;
static SemaphoreHandle_t wifi_status_mutex = NULL;
static SemaphoreHandle_t ap_list_mutex = NULL;
static bool wifi_initialized = false;
static wifi_status_t wifi_status = {0};
static volatile bool scan_in_progress = false;
static uint32_t scan_start_tick_ms = 0;
static uint32_t scan_end_tick_ms = 0;
static const uint32_t SCAN_ESTIMATED_MS = 4000; // conservative
static esp_netif_t *sta_netif = NULL;
static esp_netif_t *ap_netif = NULL;
// handles para event handler instances (para poder desregistrar corretamente)
static esp_event_handler_instance_t wifi_event_handle_inst = NULL;
static esp_event_handler_instance_t ip_event_handle_inst = NULL;

// Inicializa o mutex para sincronização WiFi
static void init_wifi_mutex() {
    if (wifi_mutex == NULL) {
        wifi_mutex = xSemaphoreCreateMutex();
        if (wifi_mutex == NULL) {
            ESP_LOGE(TAG, "Falha ao criar mutex WiFi");
        }
    }
    if (wifi_status_mutex == NULL) {
        wifi_status_mutex = xSemaphoreCreateMutex();
        if (wifi_status_mutex == NULL) {
            ESP_LOGE(TAG, "Falha ao criar mutex wifi_status");
        }
    }
    if (ap_list_mutex == NULL) {
        ap_list_mutex = xSemaphoreCreateMutex();
        if (ap_list_mutex == NULL) {
            ESP_LOGE(TAG, "Falha ao criar mutex ap_list");
        }
    }
}

// Função segura para mudar modo WiFi
static esp_err_t safe_wifi_mode_change(wifi_mode_t mode) {
    if (wifi_mutex == NULL) {
        init_wifi_mutex();
    }
    
    if (xSemaphoreTake(wifi_mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
        esp_err_t ret = esp_wifi_set_mode(mode);
        xSemaphoreGive(wifi_mutex);
        return ret;
    }
    ESP_LOGE(TAG, "Timeout ao tentar obter mutex para mudança de modo");
    return ESP_ERR_TIMEOUT;
}

// Função segura para parar WiFi
static esp_err_t safe_wifi_stop() {
    if (wifi_mutex == NULL) {
        init_wifi_mutex();
    }
    
    if (xSemaphoreTake(wifi_mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
        esp_err_t ret = esp_wifi_stop();
        xSemaphoreGive(wifi_mutex);
        return ret;
    }
    ESP_LOGE(TAG, "Timeout ao tentar obter mutex para parar WiFi");
    return ESP_ERR_TIMEOUT;
}

// Função segura para iniciar WiFi
static esp_err_t safe_wifi_start() {
    if (wifi_mutex == NULL) {
        init_wifi_mutex();
    }
    
    if (xSemaphoreTake(wifi_mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
        esp_err_t ret = esp_wifi_start();
        xSemaphoreGive(wifi_mutex);
        return ret;
    }
    ESP_LOGE(TAG, "Timeout ao tentar obter mutex para iniciar WiFi");
    return ESP_ERR_TIMEOUT;
}

// Função para atualizar status WiFi
void wifi_set_status_message(const char* message) {
    if (message != NULL) {
        if (wifi_status_mutex == NULL) init_wifi_mutex();
        if (xSemaphoreTake(wifi_status_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            strncpy(wifi_status.status_message, message, WIFI_STATUS_MSG_MAX_LEN - 1);
            wifi_status.status_message[WIFI_STATUS_MSG_MAX_LEN - 1] = '\0';
            xSemaphoreGive(wifi_status_mutex);
        }
    }
    set_wifi_status(message);
}

// Função para obter status WiFi
wifi_status_t wifi_get_status() {
    return wifi_status;
}

// Função para verificar se WiFi está inicializado
bool wifi_is_initialized() {
    return wifi_initialized;
}

// Task que inicia o webserver de forma segura (C, sem lambda)
static void start_webserver_task(void *param) {
    // aguarda um pouco para garantir que o AP esteja totalmente ativo
    vTaskDelay(pdMS_TO_TICKS(500));
    start_web_server();
    vTaskDelete(NULL);
}

static void fallback_to_ap_task(void *param) {
    ESP_LOGI(TAG, "Iniciando verificação de conexão WiFi");
    
    // Aguarda mais tempo antes de verificar conexão
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    int timeout = 60 * 1000 / 200; // 60 segundos, delay de 200ms
    for (int i = 0; i < timeout; ++i) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            ESP_LOGI(TAG, "Conexão STA bem-sucedida para %s, cancelando fallback", ap_info.ssid);
            if (wifi_status_mutex == NULL) init_wifi_mutex();
            if (xSemaphoreTake(wifi_status_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                wifi_status.is_connected = true;
                strncpy(wifi_status.current_ssid, (char*)ap_info.ssid, WIFI_SSID_MAX_LEN - 1);
                wifi_status.current_ssid[WIFI_SSID_MAX_LEN - 1] = '\0';
                wifi_status.rssi = ap_info.rssi;
                xSemaphoreGive(wifi_status_mutex);
            }
            wifi_set_status_message("Conectado com sucesso!");
            vTaskDelete(NULL);
            fallback_task_handle = NULL;
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    
    ESP_LOGI(TAG, "Timeout atingido, mantendo modo APSTA mas desconectando STA");
    
    // Para a task de fallback se ainda existir
    if (fallback_task_handle != NULL) {
        fallback_task_handle = NULL;
    }
    
    // Apenas desconecta STA, mantém AP ativo
    esp_wifi_disconnect();
    wifi_status.is_connected = false;
    wifi_status.current_ssid[0] = '\0';
    wifi_status.ip_address[0] = '\0';
    wifi_set_status_message("Conexão falhou - AP mantido ativo");
    ESP_LOGI(TAG, "STA desconectado, AP continua ativo");
    
    vTaskDelete(NULL);
}

esp_err_t wifi_scan(void) {
    ESP_LOGI(TAG, "Iniciando scan WiFi");

    if (!wifi_initialized) {
        ESP_LOGE(TAG, "WiFi não inicializado, pulando scan");
        return ESP_ERR_INVALID_STATE;
    }

    // Limpa resultados anteriores (protegido)
    if (ap_list_mutex == NULL) init_wifi_mutex();
    if (xSemaphoreTake(ap_list_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        ap_count = 0;
        memset(ap_records, 0, sizeof(ap_records));
        xSemaphoreGive(ap_list_mutex);
    } else {
        ESP_LOGW(TAG, "Timeout ao obter mutex ap_list para limpar resultados anteriores");
        // ainda prossegue sem bloquear
        ap_count = 0;
        memset(ap_records, 0, sizeof(ap_records));
    }

    // Salva modo atual para restaurar depois
    wifi_mode_t old_mode;
    esp_wifi_get_mode(&old_mode);

    // Garante que STA está ativo para o scan.
    // Se estivermos em AP-only, trocamos para APSTA (mantendo o AP) para
    // permitir escanear sem derrubar o ponto de acesso.
    if (old_mode != WIFI_MODE_STA && old_mode != WIFI_MODE_APSTA) {
        wifi_mode_t tmp_mode = WIFI_MODE_STA;
        if (old_mode == WIFI_MODE_AP) {
            ESP_LOGI(TAG, "Dispositivo em AP-only; tentando ativar APSTA para scan sem parar o WiFi (mantendo AP)");
            tmp_mode = WIFI_MODE_APSTA;
        } else {
            ESP_LOGI(TAG, "Tentando trocar temporariamente para modo STA para scan sem parar o WiFi");
        }

        // Tentar trocar o modo sem parar o WiFi para evitar derrubar conexões TCP (ex.: HTTP)
        esp_err_t mr = safe_wifi_mode_change(tmp_mode);
        if (mr != ESP_OK) {
            ESP_LOGW(TAG, "Alteração de modo in-place falhou (%s); realizando stop/start como fallback", esp_err_to_name(mr));
            // fallback conservador: parar e iniciar
            safe_wifi_stop();
            vTaskDelay(pdMS_TO_TICKS(200));
            safe_wifi_mode_change(tmp_mode);
            safe_wifi_start();
            vTaskDelay(pdMS_TO_TICKS(200));
        } else {
            ESP_LOGI(TAG, "Modo alterado in-place para %d com sucesso", (int)tmp_mode);
        }
    }

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true
    };

    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao iniciar scan: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_scan_get_ap_num(&ap_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao obter numero de APs do scan: %s", esp_err_to_name(ret));
        ap_count = 0;
    }
    if (ap_count > MAX_APs) ap_count = MAX_APs;

    if (ap_count > 0) {
        // Obtem registros localmente e depois copia para a estrutura global protegida
        wifi_ap_record_t local_records[MAX_APs];
        uint16_t local_count = ap_count;
        ret = esp_wifi_scan_get_ap_records(&local_count, local_records);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Erro ao obter registros AP: %s", esp_err_to_name(ret));
            // reseta ap_count global
            if (ap_list_mutex == NULL) init_wifi_mutex();
            if (xSemaphoreTake(ap_list_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                ap_count = 0;
                xSemaphoreGive(ap_list_mutex);
            } else {
                ap_count = 0;
            }
        } else {
            // copia para a estrutura global protegida
            if (ap_list_mutex == NULL) init_wifi_mutex();
            if (xSemaphoreTake(ap_list_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                if (local_count > MAX_APs) local_count = MAX_APs;
                memcpy(ap_records, local_records, sizeof(wifi_ap_record_t) * local_count);
                ap_count = local_count;
                xSemaphoreGive(ap_list_mutex);
            } else {
                ESP_LOGW(TAG, "Timeout ao obter mutex ap_list para copiar resultados do scan");
                // mesmo sem mutex, atualiza com cautela
                if (local_count > MAX_APs) local_count = MAX_APs;
                memcpy(ap_records, local_records, sizeof(wifi_ap_record_t) * local_count);
                ap_count = local_count;
            }
            ESP_LOGI(TAG, "Scan encontrou %d redes", ap_count);
        }
    } else {
        ESP_LOGW(TAG, "Nenhuma rede encontrada no scan");
    }

    // Restaura modo original, se foi alterado
    if (old_mode != WIFI_MODE_STA && old_mode != WIFI_MODE_APSTA) {
        esp_err_t mr = safe_wifi_mode_change(old_mode);
        if (mr != ESP_OK) {
            ESP_LOGW(TAG, "Restauração de modo in-place falhou (%s); realizando stop/start como fallback", esp_err_to_name(mr));
            safe_wifi_stop();
            vTaskDelay(pdMS_TO_TICKS(200));
            safe_wifi_mode_change(old_mode);
            safe_wifi_start();
        } else {
            ESP_LOGI(TAG, "Modo restaurado in-place para %d com sucesso", (int)old_mode);
        }
    }

    return ESP_OK;
}

// Worker used for asynchronous scans
static void wifi_scan_worker(void *param) {
    (void)param;
    scan_in_progress = true;
    scan_start_tick_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    // perform blocking scan
    wifi_scan();
    scan_end_tick_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    scan_in_progress = false;
    vTaskDelete(NULL);
}

esp_err_t wifi_start_scan_async(void) {
    if (scan_in_progress) return ESP_ERR_INVALID_STATE;
    BaseType_t ok = xTaskCreate(wifi_scan_worker, "wifi_scan_worker", 8192, NULL, 5, NULL);
    if (ok != pdPASS) return ESP_FAIL;
    return ESP_OK;
}

// Worker that waits for STA to become connected within timeout_ms, then switches
// the device to STA-only mode (disables AP) to leave only the STA interface up.
static void wifi_switch_monitor_task(void *param) {
    uint32_t timeout_ms = (uint32_t)(uintptr_t)param;
    uint32_t start = xTaskGetTickCount() * portTICK_PERIOD_MS;
    uint32_t now = start;
    ESP_LOGI(TAG, "wifi_switch_monitor_task started, timeout %ums", (unsigned)timeout_ms);
    while (now - start < timeout_ms) {
        bool connected = false;
        bool has_ip = false;
        if (wifi_status_mutex == NULL) init_wifi_mutex();
        if (xSemaphoreTake(wifi_status_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            connected = wifi_status.is_connected;
            has_ip = (wifi_status.ip_address[0] != '\0');
            xSemaphoreGive(wifi_status_mutex);
        }
        // Only switch to STA-only once link is up and an IP address is assigned
        if (connected && has_ip) {
            ESP_LOGI(TAG, "STA connected, switching to STA-only mode");
            // change to STA-only
            esp_err_t r = safe_wifi_mode_change(WIFI_MODE_STA);
            if (r != ESP_OK) {
                ESP_LOGW(TAG, "Failed to switch to STA mode in-place: %s; attempting stop/start", esp_err_to_name(r));
                safe_wifi_stop();
                vTaskDelay(pdMS_TO_TICKS(200));
                safe_wifi_mode_change(WIFI_MODE_STA);
                safe_wifi_start();
            }
            // Optionally stop AP netif if needed
            ESP_LOGI(TAG, "Switched to STA-only mode");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    }
    ESP_LOGI(TAG, "wifi_switch_monitor_task ending");
    vTaskDelete(NULL);
}

esp_err_t wifi_switch_to_sta_on_successful_connect(uint32_t timeout_ms) {
    if (timeout_ms == 0) timeout_ms = 15000; // default 15s
    BaseType_t ok = xTaskCreate(wifi_switch_monitor_task, "wifi_switch_monitor", 4096, (void*)(uintptr_t)timeout_ms, 5, NULL);
    if (ok != pdPASS) return ESP_FAIL;
    return ESP_OK;
}

// Worker to switch to STA-only mode (runs out of event handler context)
static void ap_disable_worker(void *param) {
    (void)param;
    ESP_LOGI(TAG, "ap_disable_worker: switching to STA-only mode now");
    esp_err_t r = safe_wifi_mode_change(WIFI_MODE_STA);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "ap_disable_worker: failed in-place (%s), attempting stop/start", esp_err_to_name(r));
        safe_wifi_stop();
        vTaskDelay(pdMS_TO_TICKS(200));
        safe_wifi_mode_change(WIFI_MODE_STA);
        safe_wifi_start();
    }
    ESP_LOGI(TAG, "ap_disable_worker: completed switch to STA-only mode");
    ap_disable_task_handle = NULL;
    vTaskDelete(NULL);
}

// Public-ish wrapper to schedule AP disable task (safe to call from event handler)
static void wifi_disable_ap_now(void) {
    if (ap_disable_task_handle != NULL) {
        ESP_LOGI(TAG, "wifi_disable_ap_now: AP-disable task already running");
        return;
    }
    BaseType_t ok = xTaskCreate(ap_disable_worker, "ap_disable_worker", 4096, NULL, 5, &ap_disable_task_handle);
    if (ok != pdPASS) {
        ESP_LOGW(TAG, "wifi_disable_ap_now: failed to create task, attempting direct mode change");
        esp_err_t r = safe_wifi_mode_change(WIFI_MODE_STA);
        if (r != ESP_OK) {
            safe_wifi_stop();
            vTaskDelay(pdMS_TO_TICKS(200));
            safe_wifi_mode_change(WIFI_MODE_STA);
            safe_wifi_start();
        }
        ap_disable_task_handle = NULL;
    }
}

bool wifi_is_scan_in_progress(void) {
    return scan_in_progress;
}

uint32_t wifi_scan_time_left_ms(void) {
    if (!scan_in_progress) return 0;
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    uint32_t elapsed = (now > scan_start_tick_ms) ? (now - scan_start_tick_ms) : 0;
    if (elapsed >= SCAN_ESTIMATED_MS) return 0;
    return SCAN_ESTIMATED_MS - elapsed;
}

uint32_t wifi_last_scan_duration_ms(void) {
    if (scan_start_tick_ms == 0 || scan_end_tick_ms == 0) return 0;
    return (scan_end_tick_ms > scan_start_tick_ms) ? (scan_end_tick_ms - scan_start_tick_ms) : 0;
}


// Handler para eventos WiFi
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "AP iniciado");
        if (wifi_status_mutex == NULL) init_wifi_mutex();
        if (xSemaphoreTake(wifi_status_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            wifi_status.ap_active = true;
            xSemaphoreGive(wifi_status_mutex);
        }
        // Cria uma task curta para iniciar o webserver após o AP estar ativo
        // Isso evita executar operações pesadas diretamente no handler de eventos
        static bool webserver_task_created = false;
        if (!webserver_task_created) {
            webserver_task_created = true;
            // cria task que chama start_web_server() e se encerra
            xTaskCreate(
                start_webserver_task,
                "start_webserver_task",
                4096,
                NULL,
                5,
                NULL
            );
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Cliente conectado ao AP: %02x:%02x:%02x:%02x:%02x:%02x", 
                 event->mac[0], event->mac[1], event->mac[2], 
                 event->mac[3], event->mac[4], event->mac[5]);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Cliente desconectado do AP: %02x:%02x:%02x:%02x:%02x:%02x", 
                 event->mac[0], event->mac[1], event->mac[2], 
                 event->mac[3], event->mac[4], event->mac[5]);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA iniciado");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "STA conectado ao AP");
        if (wifi_status_mutex == NULL) init_wifi_mutex();
        if (xSemaphoreTake(wifi_status_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            wifi_status.is_connected = true;
            xSemaphoreGive(wifi_status_mutex);
        }
        wifi_set_status_message("Conectando...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGI(TAG, "STA desconectado do AP, motivo: %d", event->reason);
        if (wifi_status_mutex == NULL) init_wifi_mutex();
        if (xSemaphoreTake(wifi_status_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            wifi_status.is_connected = false;
            wifi_status.current_ssid[0] = '\0';
            wifi_status.ip_address[0] = '\0';
            xSemaphoreGive(wifi_status_mutex);
        }
        wifi_set_status_message("Desconectado");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        // Build human-readable ip/mask/gw strings
        char ipbuf[16] = {0}, maskbuf[16] = {0}, gwbuf[16] = {0};
        snprintf(ipbuf, sizeof(ipbuf), "%lu.%lu.%lu.%lu",
                 (unsigned long)((event->ip_info.ip.addr >> 24) & 0xFF),
                 (unsigned long)((event->ip_info.ip.addr >> 16) & 0xFF),
                 (unsigned long)((event->ip_info.ip.addr >> 8) & 0xFF),
                 (unsigned long)(event->ip_info.ip.addr & 0xFF));
        snprintf(maskbuf, sizeof(maskbuf), "%lu.%lu.%lu.%lu",
                 (unsigned long)((event->ip_info.netmask.addr >> 24) & 0xFF),
                 (unsigned long)((event->ip_info.netmask.addr >> 16) & 0xFF),
                 (unsigned long)((event->ip_info.netmask.addr >> 8) & 0xFF),
                 (unsigned long)(event->ip_info.netmask.addr & 0xFF));
        snprintf(gwbuf, sizeof(gwbuf), "%lu.%lu.%lu.%lu",
                 (unsigned long)((event->ip_info.gw.addr >> 24) & 0xFF),
                 (unsigned long)((event->ip_info.gw.addr >> 16) & 0xFF),
                 (unsigned long)((event->ip_info.gw.addr >> 8) & 0xFF),
                 (unsigned long)(event->ip_info.gw.addr & 0xFF));

        ESP_LOGI(TAG, "*** STA CONECTADO COM SUCESSO ***");
        ESP_LOGI(TAG, "  IP: %s  MASK: %s  GW: %s", ipbuf, maskbuf, gwbuf);

        // Try to get AP info (SSID/rssi) for extra logging
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            char ssid_log[WIFI_SSID_MAX_LEN+1] = {0};
            strncpy(ssid_log, (char*)ap_info.ssid, sizeof(ssid_log)-1);
            ESP_LOGI(TAG, "  SSID: '%s'  RSSI: %d dBm", ssid_log, ap_info.rssi);
            ESP_LOGI(TAG, "*** MODO DUAL ATIVO: AP + STA FUNCIONANDO ***");
            if (wifi_status_mutex == NULL) init_wifi_mutex();
            if (xSemaphoreTake(wifi_status_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                strncpy(wifi_status.current_ssid, ssid_log, sizeof(wifi_status.current_ssid)-1);
                wifi_status.current_ssid[sizeof(wifi_status.current_ssid)-1] = '\0';
                wifi_status.rssi = ap_info.rssi;
                wifi_status.is_connected = true;
                xSemaphoreGive(wifi_status_mutex);
            }
        } else {
            ESP_LOGW(TAG, "Não foi possível obter informações do AP conectado (esp_wifi_sta_get_ap_info falhou)");
            if (wifi_status_mutex == NULL) init_wifi_mutex();
            if (xSemaphoreTake(wifi_status_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                strncpy(wifi_status.ip_address, ipbuf, sizeof(wifi_status.ip_address)-1);
                wifi_status.ip_address[sizeof(wifi_status.ip_address)-1] = '\0';
                wifi_status.is_connected = true;
                xSemaphoreGive(wifi_status_mutex);
            }
        }

        // Update stored IP address and status message
        if (wifi_status_mutex == NULL) init_wifi_mutex();
        if (xSemaphoreTake(wifi_status_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            strncpy(wifi_status.ip_address, ipbuf, sizeof(wifi_status.ip_address)-1);
            wifi_status.ip_address[sizeof(wifi_status.ip_address)-1] = '\0';
            xSemaphoreGive(wifi_status_mutex);
        }
        char status_msg[WIFI_STATUS_MSG_MAX_LEN];
        snprintf(status_msg, sizeof(status_msg), "Conectado com sucesso! IP: %s", ipbuf);
        wifi_set_status_message(status_msg);
        // Schedule AP disable now that STA has IP
        wifi_disable_ap_now();
    }
}

void start_wifi_ap() {
    ESP_LOGI(TAG, "Iniciando WiFi AP");
    
    // Inicializa mutex se necessário
    init_wifi_mutex();
    
    // Inicializa componentes WiFi
    esp_netif_init();
    esp_event_loop_create_default();
    
    // Cria interfaces de rede
    ap_netif = esp_netif_create_default_wifi_ap();
    sta_netif = esp_netif_create_default_wifi_sta();
    
    if (ap_netif == NULL || sta_netif == NULL) {
        ESP_LOGE(TAG, "Falha ao criar interfaces de rede");
        return;
    }
    
    // Registra handlers de eventos
    // registra e guarda as instâncias para permitir unregister seguro
    esp_err_t ehret;
    ehret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &wifi_event_handle_inst);
    if (ehret != ESP_OK) {
        ESP_LOGW(TAG, "Falha ao registrar handler WiFi: %s", esp_err_to_name(ehret));
        wifi_event_handle_inst = NULL;
    }
    ehret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &ip_event_handle_inst);
    if (ehret != ESP_OK) {
        ESP_LOGW(TAG, "Falha ao registrar handler IP: %s", esp_err_to_name(ehret));
        ip_event_handle_inst = NULL;
    }
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    // Aumenta buffers para evitar problemas de memória
    cfg.static_rx_buf_num = 16;
    cfg.dynamic_rx_buf_num = 32;
    cfg.rx_ba_win = 16;
    
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar WiFi: %s", esp_err_to_name(ret));
        return;
    }
    
    // Carregar configurações do AP do NVS
    char ap_ssid[32] = "ESP32_CONFIG";  // Valor padrão
    char ap_password[64] = "12345678";  // Valor padrão
    char ap_ip[16] = "192.168.4.1";    // Valor padrão
    
    // 🆕 PRIORIDADE 1: Tentar carregar do arquivo JSON primeiro
    ap_config_t ap_config_json;
    if (load_ap_config(&ap_config_json) == ESP_OK) {
        ESP_LOGI(TAG, "✅ Configurações AP carregadas do arquivo JSON:");
        strncpy(ap_ssid, ap_config_json.ssid, sizeof(ap_ssid)-1);
        strncpy(ap_password, ap_config_json.password, sizeof(ap_password)-1);
        strncpy(ap_ip, ap_config_json.ip, sizeof(ap_ip)-1);
        ESP_LOGI(TAG, "  SSID: %s", ap_ssid);
        ESP_LOGI(TAG, "  Password: %s", ap_password);
        ESP_LOGI(TAG, "  IP: %s", ap_ip);
    } else {
        ESP_LOGI(TAG, "📂 Arquivo JSON não encontrado, tentando NVS...");
        
        // FALLBACK: Tentar carregar do NVS
        nvs_handle_t nvs_handle;
        esp_err_t nvs_err = nvs_open("ap_config", NVS_READONLY, &nvs_handle);
        if (nvs_err == ESP_OK) {
            size_t required_size = sizeof(ap_ssid);
            esp_err_t ssid_err = nvs_get_str(nvs_handle, "ssid", ap_ssid, &required_size);
            
            required_size = sizeof(ap_password);
            esp_err_t pwd_err = nvs_get_str(nvs_handle, "password", ap_password, &required_size);
            
            required_size = sizeof(ap_ip);
            esp_err_t ip_err = nvs_get_str(nvs_handle, "ip", ap_ip, &required_size);
            
            nvs_close(nvs_handle);
            
            if (ssid_err == ESP_OK && pwd_err == ESP_OK && ip_err == ESP_OK) {
                ESP_LOGI(TAG, "✅ Configurações do AP carregadas do NVS:");
                ESP_LOGI(TAG, "  SSID: %s", ap_ssid);
                ESP_LOGI(TAG, "  Password: %s", ap_password);
                ESP_LOGI(TAG, "  IP: %s", ap_ip);
            } else {
                ESP_LOGW(TAG, "Algumas configurações do AP não encontradas no NVS, usando padrões");
            }
        } else {
            ESP_LOGW(TAG, "Não foi possível abrir NVS para configurações do AP, usando padrões");
        }
    }
    
    wifi_config_t ap_config = {
        .ap = {
            .ssid_len = strlen(ap_ssid),
            .password = "",  // Será copiado abaixo
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .channel = 1,
            .beacon_interval = 100
        }
    };
    
    // Copiar strings para a estrutura
    strcpy((char*)ap_config.ap.ssid, ap_ssid);
    strcpy((char*)ap_config.ap.password, ap_password);
    
    // Inicializa em modo AP+STA para evitar trocas de modo em runtime que derrubam conexões TCP
    safe_wifi_mode_change(WIFI_MODE_APSTA);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Por enquanto, sempre usar IP padrão para evitar problemas
    // TODO: Implementar configuração de IP personalizado no futuro
    ESP_LOGI(TAG, "Usando IP padrão do AP: 192.168.4.1");
    strcpy(ap_ip, "192.168.4.1"); // Forçar IP padrão
    
    /* COMENTADO TEMPORARIAMENTE - Configuração de IP personalizado
    if (strcmp(ap_ip, "192.168.4.1") != 0) {
        ESP_LOGI(TAG, "Configurando IP personalizado do AP: %s", ap_ip);
        
        // Parar DHCP server primeiro
        esp_netif_dhcps_stop(ap_netif);
        
        esp_netif_ip_info_t ip_info;
        esp_netif_str_to_ip4(ap_ip, &ip_info.ip);
        esp_netif_str_to_ip4("255.255.255.0", &ip_info.netmask);
        esp_netif_str_to_ip4(ap_ip, &ip_info.gw);
        
        esp_err_t ip_ret = esp_netif_set_ip_info(ap_netif, &ip_info);
        if (ip_ret == ESP_OK) {
            ESP_LOGI(TAG, "IP do AP configurado com sucesso: %s", ap_ip);
            
            // Reiniciar DHCP server
            esp_netif_dhcps_start(ap_netif);
        } else {
            ESP_LOGW(TAG, "Falha ao configurar IP do AP: %s", esp_err_to_name(ip_ret));
            // Reiniciar DHCP server mesmo em caso de erro
            esp_netif_dhcps_start(ap_netif);
        }
    }
    */

    ret = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao configurar AP: %s", esp_err_to_name(ret));
        return;
    }

    ret = safe_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar WiFi: %s", esp_err_to_name(ret));
        return;
    }
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    wifi_initialized = true;
    if (wifi_status_mutex == NULL) init_wifi_mutex();
    if (xSemaphoreTake(wifi_status_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        wifi_status.ap_active = true;
        xSemaphoreGive(wifi_status_mutex);
    }
    wifi_set_status_message("AP iniciado com sucesso");
    ESP_LOGI(TAG, "WiFi AP iniciado com sucesso");
    ESP_LOGI(TAG, "  SSID: %s", ap_ssid);
    ESP_LOGI(TAG, "  IP configurado: %s", ap_ip);
    ESP_LOGI(TAG, "  Servidor web deve estar acessível em: http://%s", ap_ip);
    
    // AP iniciado; o webserver será iniciado pelo app_main()
    ESP_LOGI(TAG, "AP ativo; start_web_server() será chamado externamente.");

    // Verificar se há configurações WiFi salvas e tentar conectar automaticamente
    ESP_LOGI(TAG, "=== VERIFICANDO CONFIGURAÇÕES WIFI SALVAS ===");
    
    char saved_ssid[64] = "";
    char saved_password[64] = "";
    bool config_found = false;
    
    // 🆕 PRIORIDADE 1: Tentar carregar do arquivo JSON primeiro
    sta_config_t sta_config_json;
    if (load_sta_config(&sta_config_json) == ESP_OK) {
        ESP_LOGI(TAG, "✅ Configurações STA carregadas do arquivo JSON:");
        strncpy(saved_ssid, sta_config_json.ssid, sizeof(saved_ssid)-1);
        strncpy(saved_password, sta_config_json.password, sizeof(saved_password)-1);
        ESP_LOGI(TAG, "  SSID: %s", saved_ssid);
        ESP_LOGI(TAG, "  Password length: %d", strlen(saved_password));
        config_found = true;
    } else {
        ESP_LOGI(TAG, "📂 Arquivo STA JSON não encontrado, tentando NVS...");
        
        // FALLBACK: Tentar carregar do NVS
        nvs_handle_t wifi_nvs_handle;
        esp_err_t wifi_nvs_err = nvs_open("wifi_config", NVS_READONLY, &wifi_nvs_handle);
        if (wifi_nvs_err == ESP_OK) {
            size_t required_size = sizeof(saved_ssid);
            
            esp_err_t ssid_err = nvs_get_str(wifi_nvs_handle, "wifi_ssid", saved_ssid, &required_size);
            required_size = sizeof(saved_password);
            esp_err_t pwd_err = nvs_get_str(wifi_nvs_handle, "wifi_password", saved_password, &required_size);
            
            nvs_close(wifi_nvs_handle);
            
            if (ssid_err == ESP_OK && strlen(saved_ssid) > 0) {
                ESP_LOGI(TAG, "✅ Configurações STA carregadas do NVS:");
                ESP_LOGI(TAG, "  SSID: %s", saved_ssid);
                ESP_LOGI(TAG, "  Password length: %d", strlen(saved_password));
                config_found = true;
            }
        }
    }
    
    if (config_found && strlen(saved_ssid) > 0) {
        
        ESP_LOGI(TAG, "*** CONFIGURAÇÃO WIFI ENCONTRADA ***");
        ESP_LOGI(TAG, "*** MODO DUAL ATIVO: AP + STA ***");
        ESP_LOGI(TAG, "  AP ativo em: %s (SSID: %s)", ap_ip, ap_ssid);
        ESP_LOGI(TAG, "  Tentando conectar STA à: %s", saved_ssid);
        
        // Conectar automaticamente à rede salva
        wifi_connect(saved_ssid, saved_password);
    } else {
        ESP_LOGI(TAG, "*** NENHUMA CONFIGURAÇÃO WIFI SALVA ***");
        ESP_LOGI(TAG, "*** MODO AP APENAS ***");
        ESP_LOGI(TAG, "  AP ativo em: %s (SSID: %s)", ap_ip, ap_ssid);
    }

    // Dispara um scan inicial em background para popular a lista de redes
    // Faz um pequeno delay para garantir que a interface STA esteja totalmente pronta
    vTaskDelay(pdMS_TO_TICKS(500));
    if (!scan_in_progress) {
        esp_err_t scan_ret = wifi_start_scan_async();
        if (scan_ret == ESP_OK) {
            ESP_LOGI(TAG, "Scan WiFi inicial disparado em background");
        } else {
            ESP_LOGW(TAG, "Falha ao disparar scan WiFi inicial: %s", esp_err_to_name(scan_ret));
        }
    } else {
        ESP_LOGI(TAG, "Scan WiFi já em andamento, não iniciando outro");
    }
}

void wifi_connect(const char* ssid, const char* password) {
    ESP_LOGI(TAG, "*** FUNÇÃO WIFI_CONNECT CHAMADA ***");
    ESP_LOGI(TAG, "  SSID: '%s' (len=%d)", ssid ? ssid : "NULL", ssid ? strlen(ssid) : 0);
    ESP_LOGI(TAG, "  Password: %s (len=%d)", password ? "***" : "NULL", password ? strlen(password) : 0);
    
    if (!wifi_initialized) {
        ESP_LOGE(TAG, "WiFi não inicializado");
        return;
    }
    
    if (ssid == NULL || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "SSID inválido");
        return;
    }
    
    // Cancela task de fallback anterior se existir
    if (fallback_task_handle != NULL) {
        vTaskDelete(fallback_task_handle);
        fallback_task_handle = NULL;
    }
    
    // Configura STA sem parar o AP
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password != NULL) {
        strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.threshold.rssi = -127;
    
    // Configura AP também para manter o webserver ativo - usar configurações salvas
    char ap_ssid[32] = "ESP32_CONFIG";  // Valor padrão
    char ap_password[64] = "12345678";  // Valor padrão
    
    // Carregar configurações do AP do NVS
    nvs_handle_t ap_nvs_handle;
    esp_err_t ap_nvs_err = nvs_open("ap_config", NVS_READONLY, &ap_nvs_handle);
    if (ap_nvs_err == ESP_OK) {
        size_t required_size = sizeof(ap_ssid);
        nvs_get_str(ap_nvs_handle, "ssid", ap_ssid, &required_size);
        required_size = sizeof(ap_password);
        nvs_get_str(ap_nvs_handle, "password", ap_password, &required_size);
        nvs_close(ap_nvs_handle);
    }
    
    wifi_config_t ap_config = {
        .ap = {
            .ssid_len = strlen(ap_ssid),
            .password = "",  // Será copiado abaixo
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        }
    };
    
    // Copiar strings para a estrutura
    strcpy((char*)ap_config.ap.ssid, ap_ssid);
    strcpy((char*)ap_config.ap.password, ap_password);
    
    // Muda para modo APSTA se não estiver
    wifi_mode_t current_mode;
    esp_wifi_get_mode(&current_mode);
    if (current_mode != WIFI_MODE_APSTA) {
        safe_wifi_stop();
        vTaskDelay(pdMS_TO_TICKS(500));
        safe_wifi_mode_change(WIFI_MODE_APSTA);
        vTaskDelay(pdMS_TO_TICKS(500));
        safe_wifi_start();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    // Configura ambas as interfaces
    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao configurar STA: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao configurar AP: %s", esp_err_to_name(ret));
        return;
    }
    
    // Conecta
    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar conexão: %s", esp_err_to_name(ret));
        return;
    }

    // Aplica IP estático automaticamente, se existir configuração
    char ip[16] = "", mask[16] = "", gw[16] = "", dns[16] = "";
    network_config_t net_config;
    if (load_network_config(&net_config) == ESP_OK) {
        strncpy(ip, net_config.ip, sizeof(ip)-1);
        strncpy(mask, net_config.mask, sizeof(mask)-1);
        strncpy(gw, net_config.gateway, sizeof(gw)-1);
        strncpy(dns, net_config.dns, sizeof(dns)-1);
        ip[sizeof(ip)-1] = '\0';
        mask[sizeof(mask)-1] = '\0';
        gw[sizeof(gw)-1] = '\0';
        dns[sizeof(dns)-1] = '\0';
    } else {
        ip[0] = mask[0] = gw[0] = dns[0] = '\0';
    }
    if (strlen(ip) > 0 && strlen(mask) > 0 && strlen(gw) > 0) {
        ESP_LOGI(TAG, "Aplicando IP estático salvo em network_config.json");
        wifi_apply_static_ip(ip, mask, gw, dns);
    } else {
        ESP_LOGI(TAG, "Nenhuma configuração de IP estático encontrada, usando DHCP.");
    }

    ESP_LOGI(TAG, "*** CONEXÃO WIFI INICIADA ***");
    ESP_LOGI(TAG, "*** MODO DUAL MANTIDO: AP + STA ***");
    ESP_LOGI(TAG, "  AP: %s (mantido ativo)", ap_ssid);
    ESP_LOGI(TAG, "  STA: Conectando à %s", ssid);
    wifi_set_status_message("Conectando...");

    // Inicia task de fallback com delay maior
    vTaskDelay(pdMS_TO_TICKS(3000));
    if (fallback_task_handle == NULL) {
        xTaskCreate(fallback_to_ap_task, "fallback_to_ap_task", 4096, NULL, 5, &fallback_task_handle);
    }
}

// Nova função para desconectar WiFi
void wifi_disconnect() {
    if (!wifi_initialized) {
        return;
    }
    
    ESP_LOGI(TAG, "Desconectando WiFi STA");
    esp_wifi_disconnect();
    
    // Cancela task de fallback se existir
    if (fallback_task_handle != NULL) {
        vTaskDelete(fallback_task_handle);
        fallback_task_handle = NULL;
    }
    
    if (wifi_status_mutex == NULL) init_wifi_mutex();
    if (xSemaphoreTake(wifi_status_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        wifi_status.is_connected = false;
        wifi_status.current_ssid[0] = '\0';
        wifi_status.ip_address[0] = '\0';
        xSemaphoreGive(wifi_status_mutex);
    }
    wifi_set_status_message("Desconectado");
}

// Nova função para limpeza de recursos
void wifi_cleanup() {
    ESP_LOGI(TAG, "Limpando recursos WiFi");
    
    // Cancela task de fallback se existir
    if (fallback_task_handle != NULL) {
        vTaskDelete(fallback_task_handle);
        fallback_task_handle = NULL;
    }
    
    // Desconecta e para WiFi
    if (wifi_initialized) {
        esp_wifi_disconnect();
        esp_wifi_stop();
        esp_wifi_deinit();
        wifi_initialized = false;
    }
    
    // Remove handlers de eventos usando as instâncias armazenadas
    if (wifi_event_handle_inst != NULL) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handle_inst);
        wifi_event_handle_inst = NULL;
    }
    if (ip_event_handle_inst != NULL) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handle_inst);
        ip_event_handle_inst = NULL;
    }
    
    // Limpa interfaces de rede
    if (ap_netif != NULL) {
        esp_netif_destroy(ap_netif);
        ap_netif = NULL;
    }
    if (sta_netif != NULL) {
        esp_netif_destroy(sta_netif);
        sta_netif = NULL;
    }
    
    // Limpa mutex
    if (wifi_mutex != NULL) {
        vSemaphoreDelete(wifi_mutex);
        wifi_mutex = NULL;
    }
    if (wifi_status_mutex != NULL) {
        vSemaphoreDelete(wifi_status_mutex);
        wifi_status_mutex = NULL;
    }
    if (ap_list_mutex != NULL) {
        vSemaphoreDelete(ap_list_mutex);
        ap_list_mutex = NULL;
    }
    
    // Limpa status (protegido)
    if (wifi_status_mutex == NULL) init_wifi_mutex();
    if (xSemaphoreTake(wifi_status_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        memset(&wifi_status, 0, sizeof(wifi_status));
        xSemaphoreGive(wifi_status_mutex);
    }
    
    ESP_LOGI(TAG, "Recursos WiFi limpos");
}

// Fornece uma cópia consistente e rápida dos resultados do último scan.
void wifi_get_ap_list_snapshot(wifi_ap_record_t *out_records, uint16_t *out_count) {
    if (!out_records || !out_count) return;
    if (ap_list_mutex == NULL) init_wifi_mutex();
    if (xSemaphoreTake(ap_list_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        uint16_t c = ap_count;
        if (c > MAX_APs) c = MAX_APs;
        memcpy(out_records, ap_records, sizeof(wifi_ap_record_t) * c);
        *out_count = c;
        xSemaphoreGive(ap_list_mutex);
    } else {
        // Timeout: return empty
        *out_count = 0;
    }
}

// Função para configurar AP
esp_err_t wifi_set_ap_config(const char* ssid, const char* password, uint8_t channel) {
    if (!wifi_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (ssid == NULL || strlen(ssid) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    wifi_config_t ap_config = {0};
    strncpy((char*)ap_config.ap.ssid, ssid, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = strlen(ssid);
    
    if (password != NULL && strlen(password) >= 8) {
        strncpy((char*)ap_config.ap.password, password, sizeof(ap_config.ap.password) - 1);
        ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    } else {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    
    ap_config.ap.channel = channel;
    ap_config.ap.max_connection = 4;
    ap_config.ap.beacon_interval = 100;
    
    return esp_wifi_set_config(WIFI_IF_AP, &ap_config);
}

// Função para configurar STA
esp_err_t wifi_set_sta_config(const char* ssid, const char* password) {
    if (!wifi_initialized) {
        ESP_LOGE(TAG, "WiFi não inicializado");
        return ESP_ERR_INVALID_STATE;
    }
    
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password != NULL) {
        strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.threshold.rssi = -127;
    
    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao configurar STA: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

esp_err_t wifi_apply_static_ip(const char* ip, const char* netmask, const char* gateway, const char* dns) {
    ESP_LOGI(TAG, "Aplicando configuração de IP estático: IP=%s, MASK=%s, GW=%s, DNS=%s", 
             ip, netmask, gateway, dns);
    
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) {
        ESP_LOGE(TAG, "Interface STA não encontrada");
        return ESP_ERR_NOT_FOUND;
    }
    
    // Desabilita DHCP
    esp_err_t ret = esp_netif_dhcpc_stop(netif);
    if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGE(TAG, "Erro ao parar DHCP: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configura IP estático
    esp_netif_ip_info_t ip_info = {0};
    
    // Converte strings para endereços IP
    if (esp_netif_str_to_ip4(ip, &ip_info.ip) != ESP_OK) {
        ESP_LOGE(TAG, "IP inválido: %s", ip);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (esp_netif_str_to_ip4(netmask, &ip_info.netmask) != ESP_OK) {
        ESP_LOGE(TAG, "Máscara inválida: %s", netmask);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (esp_netif_str_to_ip4(gateway, &ip_info.gw) != ESP_OK) {
        ESP_LOGE(TAG, "Gateway inválido: %s", gateway);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Aplica configuração de IP
    ret = esp_netif_set_ip_info(netif, &ip_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao configurar IP: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configura DNS se fornecido
    if (dns != NULL && strlen(dns) > 0) {
        esp_netif_dns_info_t dns_info = {0};
        if (esp_netif_str_to_ip4(dns, &dns_info.ip.u_addr.ip4) == ESP_OK) {
            ret = esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Erro ao configurar DNS: %s", esp_err_to_name(ret));
                return ret;
            }
        } else {
            ESP_LOGE(TAG, "DNS inválido: %s", dns);
            return ESP_ERR_INVALID_ARG;
        }
    }
    
    ESP_LOGI(TAG, "Configuração de IP estático aplicada com sucesso");
    wifi_set_status_message("IP estático configurado com sucesso");
    
    return ESP_OK;
}

esp_err_t wifi_apply_dhcp() {
    ESP_LOGI(TAG, "Aplicando configuração DHCP");
    
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) {
        ESP_LOGE(TAG, "Interface STA não encontrada");
        return ESP_ERR_NOT_FOUND;
    }
    
    // Inicia DHCP
    esp_err_t ret = esp_netif_dhcpc_start(netif);
    if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
        ESP_LOGE(TAG, "Erro ao iniciar DHCP: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Configuração DHCP aplicada com sucesso");
    wifi_set_status_message("DHCP ativado com sucesso");
    
    return ESP_OK;
} 