/*
 * ========================================================================
 * MQTT CLIENT TASK - PUBLICAÇÃO DOS DADOS DA SONDA LAMBDA
 * ========================================================================
 * 
 * Este arquivo implementa um cliente MQTT para enviar os dados da sonda
 * lambda para brokers públicos como HiveMQ.
 * 
 * FUNCIONALIDADES:
 * - Conexão automática com broker MQTT
 * - Publicação dos dados da sonda em tempo real
 * - Reconexão automática em caso de falha
 * - Configuração dinâmica via interface web
 * - Integração com a máquina de estados principal
 * 
 * TÓPICOS MQTT:
 * - esp32/sonda_lambda/heat - Valor do aquecedor
 * - esp32/sonda_lambda/lambda - Valor do sensor lambda
 * - esp32/sonda_lambda/o2 - Percentual de oxigênio
 * - esp32/sonda_lambda/error - Erro do controlador
 * - esp32/sonda_lambda/output - Saída PID
 * - esp32/sonda_lambda/data - Todos os dados em JSON
 * 
 * ========================================================================
 */

#include "mqtt_client_task.h"
#include "config_manager.h"
#include "mqtt_client.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include <ctype.h>
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "MQTT_CLIENT";

// Variáveis globais
static esp_mqtt_client_handle_t mqtt_client = NULL;
static mqtt_config_t mqtt_config = {0};
static mqtt_state_t mqtt_state = MQTT_STATE_DISCONNECTED;
static SemaphoreHandle_t mqtt_mutex = NULL;
static QueueHandle_t mqtt_data_queue = NULL;
static TaskHandle_t mqtt_task_handle = NULL;
static mqtt_data_callback_t data_callback = NULL;
// Buffer e estado global para o CA PEM carregado a partir do SPIFFS
static char *g_ca_pem_buf = NULL;
static bool g_spiffs_mounted = false;

// protótipo do event handler (definido abaixo) - necessário para registro
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

// Helper: cria o esp_mqtt_client a partir da configuração global mqtt_config
static esp_err_t mqtt_create_client_from_config(void) {
    if (mqtt_client) {
        ESP_LOGW(TAG, "mqtt_client já existe, destruindo antes de recriar");
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
    }

    esp_mqtt_client_config_t mqtt_cfg = {0};

    // sanitize broker_url into local buffer
    char broker_uri_buf[256] = {0};
    if (mqtt_config.broker_url[0] != '\0') {
        size_t i = 0, j = 0;
        while (mqtt_config.broker_url[i] != '\0' && isspace((unsigned char)mqtt_config.broker_url[i])) i++;
        while (mqtt_config.broker_url[i] != '\0' && j < sizeof(broker_uri_buf)-1) broker_uri_buf[j++] = mqtt_config.broker_url[i++];
        broker_uri_buf[j] = '\0';
        while (broker_uri_buf[0] == ':') memmove(broker_uri_buf, broker_uri_buf + 1, strlen(broker_uri_buf));
        if (mqtt_config.tls_enabled) {
            if (strncmp(broker_uri_buf, "mqtt://", 7) == 0) {
                memmove(broker_uri_buf + 7, broker_uri_buf + 7, strlen(broker_uri_buf + 7) + 1);
                memcpy(broker_uri_buf, "mqtts://", 8);
            } else if (strncmp(broker_uri_buf, "mqtts://", 8) != 0) {
                char tmp[512];
                snprintf(tmp, sizeof(tmp), "mqtts://%s", broker_uri_buf);
                strncpy(broker_uri_buf, tmp, sizeof(broker_uri_buf)-1);
                broker_uri_buf[sizeof(broker_uri_buf)-1] = '\0';
            }
        }
    }

    ESP_LOGI(TAG, "MQTT broker URI (sanitizado) = '%s'", broker_uri_buf[0] ? broker_uri_buf : mqtt_config.broker_url);

    mqtt_cfg.broker.address.uri = (broker_uri_buf[0] != '\0') ? broker_uri_buf : mqtt_config.broker_url;
    mqtt_cfg.broker.address.port = mqtt_config.port;
    mqtt_cfg.credentials.client_id = mqtt_config.client_id;
    mqtt_cfg.session.keepalive = MQTT_KEEPALIVE;
    mqtt_cfg.session.disable_clean_session = false;
    mqtt_cfg.network.disable_auto_reconnect = false;
    mqtt_cfg.network.reconnect_timeout_ms = 5000;
    mqtt_cfg.network.timeout_ms = 10000;
    mqtt_cfg.buffer.size = 1024;
    mqtt_cfg.buffer.out_size = 1024;

    // Load CA PEM if TLS enabled
    g_ca_pem_buf = NULL;
    if (mqtt_config.tls_enabled) {
        ESP_LOGI(TAG, "MQTT TLS habilitado");
        if (mqtt_config.ca_path[0] != '\0') {
            esp_vfs_spiffs_conf_t conf = {
                .base_path = "/spiffs",
                .partition_label = NULL,
                .max_files = 5,
                .format_if_mount_failed = false
            };
            esp_err_t err = esp_vfs_spiffs_register(&conf);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "SPIFFS montado para ler CA: %s", mqtt_config.ca_path);
                FILE *f = fopen(mqtt_config.ca_path, "r");
                if (f) {
                    fseek(f, 0, SEEK_END);
                    long len = ftell(f);
                    fseek(f, 0, SEEK_SET);
                    if (len > 0) {
                        g_ca_pem_buf = malloc(len + 1);
                        if (g_ca_pem_buf) {
                            fread(g_ca_pem_buf, 1, len, f);
                            g_ca_pem_buf[len] = '\0';
                            ESP_LOGI(TAG, "CA PEM carregado (%ld bytes)", len);
                        } else {
                            ESP_LOGE(TAG, "Falha ao alocar buffer para CA PEM");
                        }
                    } else {
                        ESP_LOGE(TAG, "Arquivo CA PEM vazio ou inválido: %s", mqtt_config.ca_path);
                    }
                    fclose(f);
                    g_spiffs_mounted = true;
                } else {
                    ESP_LOGE(TAG, "Falha ao abrir arquivo CA PEM: %s", mqtt_config.ca_path);
                }
            } else {
                ESP_LOGE(TAG, "Falha ao montar SPIFFS: %s", esp_err_to_name(err));
            }
        } else {
            ESP_LOGW(TAG, "TLS habilitado mas mqtt_config.ca_path vazio");
        }
        if (g_ca_pem_buf) {
            mqtt_cfg.broker.verification.certificate = g_ca_pem_buf;
            mqtt_cfg.broker.verification.certificate_len = 0;
            mqtt_cfg.broker.verification.use_global_ca_store = false;
            ESP_LOGI(TAG, "CA PEM atribuído a broker.verification.certificate (len=0)");
        }
    }

    // Add credentials if set
    if (strlen(mqtt_config.username) > 0) mqtt_cfg.credentials.username = mqtt_config.username;
    if (strlen(mqtt_config.password) > 0) mqtt_cfg.credentials.authentication.password = mqtt_config.password;

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!mqtt_client) {
        ESP_LOGE(TAG, "Falha ao inicializar cliente MQTT");
        if (g_ca_pem_buf) { free(g_ca_pem_buf); g_ca_pem_buf = NULL; }
        if (g_spiffs_mounted) { esp_vfs_spiffs_unregister(NULL); g_spiffs_mounted = false; }
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao registrar event handler MQTT: %s", esp_err_to_name(ret));
        if (g_ca_pem_buf) { free(g_ca_pem_buf); g_ca_pem_buf = NULL; }
        if (g_spiffs_mounted) { esp_vfs_spiffs_unregister(NULL); g_spiffs_mounted = false; }
        return ret;
    }

    ESP_LOGI(TAG, "Cliente MQTT inicializado com sucesso (create_client)");
    return ESP_OK;
}

// Configuração padrão
static void mqtt_set_default_config(void) {
    strcpy(mqtt_config.broker_url, MQTT_BROKER_URL);
    strcpy(mqtt_config.client_id, MQTT_CLIENT_ID);
    strcpy(mqtt_config.username, ""); // Público, sem autenticação
    strcpy(mqtt_config.password, "");
    mqtt_config.port = MQTT_PORT;
    mqtt_config.qos = MQTT_QOS_LEVEL;
    mqtt_config.retain = MQTT_RETAIN;
    mqtt_config.tls_enabled = false;
    mqtt_config.ca_path[0] = '\0';
    mqtt_config.enabled = true;
    mqtt_config.publish_interval_ms = 1000; // 1 segundo
}

// Event handler para MQTT
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Conectado ao broker: %s", mqtt_config.broker_url);
            mqtt_state = MQTT_STATE_CONNECTED;
            
            // Publica mensagem de status
            esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_STATUS, "online", 0, 1, true);
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT Desconectado do broker");
            mqtt_state = MQTT_STATE_DISCONNECTED;
            break;
            
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT Inscrito em tópico, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT Desinscrito de tópico, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "MQTT Mensagem publicada, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT Dados recebidos");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            break;
            
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT Erro: %s", esp_err_to_name(event->error_handle->error_type));
            mqtt_state = MQTT_STATE_ERROR;
            break;
            
        default:
            ESP_LOGD(TAG, "MQTT Evento não tratado: %ld", event_id);
            break;
    }
}

// Inicialização do cliente MQTT
esp_err_t mqtt_init(void) {
    ESP_LOGI(TAG, "Inicializando cliente MQTT");
    
    // Configura valores padrão primeiro
    mqtt_set_default_config();
    
    // 🆕 Tentar carregar configurações do arquivo JSON
    mqtt_config_t mqtt_config_from_file;
    if (load_mqtt_config(&mqtt_config_from_file) == ESP_OK) {
        ESP_LOGI(TAG, "✅ Configurações MQTT carregadas do arquivo JSON");
        // Sobrescreve a configuração padrão com dados do arquivo
        mqtt_config = mqtt_config_from_file;
        ESP_LOGI(TAG, "  Broker: %s", mqtt_config.broker_url);
        ESP_LOGI(TAG, "  Client ID: %s", mqtt_config.client_id);
        ESP_LOGI(TAG, "  Enabled: %s", mqtt_config.enabled ? "true" : "false");
    } else {
        ESP_LOGI(TAG, "📂 Arquivo MQTT JSON não encontrado, usando valores padrão");
    }
    
    // Cria mutex e fila
    mqtt_mutex = xSemaphoreCreateMutex();
    if (!mqtt_mutex) {
        ESP_LOGE(TAG, "Falha ao criar mutex MQTT");
        return ESP_ERR_NO_MEM;
    }
    
    mqtt_data_queue = xQueueCreate(10, sizeof(sonda_data_t));
    if (!mqtt_data_queue) {
        ESP_LOGE(TAG, "Falha ao criar fila de dados MQTT");
        return ESP_ERR_NO_MEM;
    }
    
    // Configura cliente MQTT via função auxiliar (usa mqtt_config atual)
    esp_err_t cret = mqtt_create_client_from_config();
    if (cret != ESP_OK) return cret;
    
    ESP_LOGI(TAG, "Cliente MQTT inicializado com sucesso");
    // Observação: ca_pem_buf permanece alocado enquanto o cliente usa o ponteiro.
    // É responsabilidade do destrutor/stop liberar.
    return ESP_OK;
}

// Inicia conexão MQTT
esp_err_t mqtt_start(void) {
    if (!mqtt_client) {
        ESP_LOGE(TAG, "Cliente MQTT não inicializado");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!mqtt_config.enabled) {
        ESP_LOGW(TAG, "MQTT desabilitado na configuração");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Iniciando conexão MQTT com %s", mqtt_config.broker_url);
    mqtt_state = MQTT_STATE_CONNECTING;
    
    esp_err_t ret = esp_mqtt_client_start(mqtt_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar cliente MQTT: %s", esp_err_to_name(ret));
        mqtt_state = MQTT_STATE_ERROR;
    }
    
    return ret;
}

// Para conexão MQTT
esp_err_t mqtt_stop(void) {
    if (!mqtt_client) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Parando cliente MQTT");
    
    // Publica mensagem de status offline
    if (mqtt_state == MQTT_STATE_CONNECTED) {
        esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_STATUS, "offline", 0, 1, true);
        vTaskDelay(pdMS_TO_TICKS(100)); // Aguarda publicação
    }
    
    esp_err_t ret = esp_mqtt_client_stop(mqtt_client);
    mqtt_state = MQTT_STATE_DISCONNECTED;

    // Se carregamos CA a partir do SPIFFS, liberar o buffer e desmontar SPIFFS
    if (g_ca_pem_buf) {
        free(g_ca_pem_buf);
        g_ca_pem_buf = NULL;
    }
    if (g_spiffs_mounted) {
        esp_vfs_spiffs_unregister(NULL);
        g_spiffs_mounted = false;
    }

    return ret;
}

// Reinicia conexão MQTT
esp_err_t mqtt_restart(void) {
    ESP_LOGI(TAG, "Reiniciando cliente MQTT");
    esp_err_t ret = mqtt_stop();
    if (ret == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(1000)); // Aguarda 1 segundo
        ret = mqtt_start();
    }
    return ret;
}

// Função auxiliar para liberar recursos do CA/SPIFFS caso necessário
static void mqtt_free_ca_resources(void) {
    if (g_ca_pem_buf) {
        free(g_ca_pem_buf);
        g_ca_pem_buf = NULL;
    }
    if (g_spiffs_mounted) {
        esp_vfs_spiffs_unregister(NULL);
        g_spiffs_mounted = false;
    }
}

// Publica dados individuais da sonda
esp_err_t mqtt_publish_individual_values(int16_t heat, int16_t lambda, int16_t error, uint16_t o2, uint32_t output) {
    if (!mqtt_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    char payload[32];
    esp_err_t ret = ESP_OK;
    
    // Publica heat
    snprintf(payload, sizeof(payload), "%d", heat);
    if (esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_HEAT, payload, 0, mqtt_config.qos, mqtt_config.retain) == -1) {
        ret = ESP_FAIL;
    }
    
    // Publica lambda
    snprintf(payload, sizeof(payload), "%d", lambda);
    if (esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_LAMBDA, payload, 0, mqtt_config.qos, mqtt_config.retain) == -1) {
        ret = ESP_FAIL;
    }
    
    // Publica error
    snprintf(payload, sizeof(payload), "%d", error);
    if (esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_ERROR, payload, 0, mqtt_config.qos, mqtt_config.retain) == -1) {
        ret = ESP_FAIL;
    }
    
    // Publica O2
    snprintf(payload, sizeof(payload), "%u", o2);
    if (esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_O2, payload, 0, mqtt_config.qos, mqtt_config.retain) == -1) {
        ret = ESP_FAIL;
    }
    
    // Publica output
    snprintf(payload, sizeof(payload), "%lu", (unsigned long)output);
    if (esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_OUTPUT, payload, 0, mqtt_config.qos, mqtt_config.retain) == -1) {
        ret = ESP_FAIL;
    }
    
    return ret;
}

// Publica dados completos em JSON
esp_err_t mqtt_publish_sonda_data(const sonda_data_t *data) {
    if (!mqtt_is_connected() || !data || !data->valid) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Cria JSON com os dados
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        ESP_LOGE(TAG, "Falha ao criar objeto JSON");
        return ESP_ERR_NO_MEM;
    }
    
    cJSON_AddNumberToObject(json, "heat", data->heat_value);
    cJSON_AddNumberToObject(json, "lambda", data->lambda_value);
    cJSON_AddNumberToObject(json, "error", data->error_value);
    cJSON_AddNumberToObject(json, "o2", data->o2_percent);
    cJSON_AddNumberToObject(json, "output", data->output_value);
    cJSON_AddNumberToObject(json, "timestamp", data->timestamp_ms);
    cJSON_AddStringToObject(json, "device_id", mqtt_config.client_id);
    
    char *json_string = cJSON_Print(json);
    if (!json_string) {
        ESP_LOGE(TAG, "Falha ao serializar JSON");
        cJSON_Delete(json);
        return ESP_ERR_NO_MEM;
    }
    
    // Publica JSON completo
    int msg_id = esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_ALL_DATA, json_string, 0, mqtt_config.qos, mqtt_config.retain);
    
    // Publica valores individuais também
    mqtt_publish_individual_values(data->heat_value, data->lambda_value, data->error_value, data->o2_percent, data->output_value);
    
    ESP_LOGD(TAG, "Dados publicados via MQTT: %s", json_string);
    
    // Limpa memória
    free(json_string);
    cJSON_Delete(json);
    
    return (msg_id != -1) ? ESP_OK : ESP_FAIL;
}

// Verifica se está conectado
bool mqtt_is_connected(void) {
    return (mqtt_state == MQTT_STATE_CONNECTED);
}

// Retorna estado atual
mqtt_state_t mqtt_get_state(void) {
    return mqtt_state;
}

// Configura callback de dados
esp_err_t mqtt_set_data_callback(mqtt_data_callback_t callback) {
    data_callback = callback;
    return ESP_OK;
}

// Task principal do MQTT
void mqtt_client_task(void *pvParameters) {
    ESP_LOGI(TAG, "MQTT Client Task iniciada");
    
    sonda_data_t data;
    TickType_t last_publish = 0;
    
    while (1) {
        // Verifica se há dados na fila
        if (xQueueReceive(mqtt_data_queue, &data, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (mqtt_is_connected() && data.valid) {
                esp_err_t ret = mqtt_publish_sonda_data(&data);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "Falha ao publicar dados MQTT: %s", esp_err_to_name(ret));
                }
                
                // Chama callback se configurado
                if (data_callback) {
                    data_callback(&data);
                }
            }
        }
        
        // Verifica reconexão se necessário
        if (mqtt_config.enabled && mqtt_state == MQTT_STATE_DISCONNECTED) {
            TickType_t now = xTaskGetTickCount();
            if (now - last_publish > pdMS_TO_TICKS(10000)) { // Tenta reconectar a cada 10s
                ESP_LOGI(TAG, "Tentando reconectar MQTT...");
                mqtt_start();
                last_publish = now;
            }
        }
        
        // Delay para não sobrecarregar
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Função para outras tasks enviarem dados para o MQTT
esp_err_t mqtt_send_data_to_queue(int16_t heat, int16_t lambda, int16_t error, uint16_t o2, uint32_t output) {
    if (!mqtt_data_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    
    sonda_data_t data = {
        .heat_value = heat,
        .lambda_value = lambda,
        .error_value = error,
        .o2_percent = o2,
        .output_value = output,
        .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
        .valid = true
    };
    
    BaseType_t result = xQueueSend(mqtt_data_queue, &data, 0);
    return (result == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

// Configurar MQTT
esp_err_t mqtt_set_config(const mqtt_config_t *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(mqtt_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        memcpy(&mqtt_config, config, sizeof(mqtt_config_t));
        xSemaphoreGive(mqtt_mutex);
        
        // Reinicia se já estava conectado
        // Recreate client with new config
        if (mqtt_client) {
            // Stop and destroy existing client
            esp_mqtt_client_destroy(mqtt_client);
            mqtt_client = NULL;
        }
        // Free any previously loaded CA resources
        mqtt_free_ca_resources();

        // Create new client using updated mqtt_config
        esp_err_t cret = mqtt_create_client_from_config();
        if (cret != ESP_OK) {
            ESP_LOGE(TAG, "Falha ao recriar cliente MQTT com nova configuração: %s", esp_err_to_name(cret));
            return cret;
        }

        // If enabled, start the client
        if (mqtt_config.enabled) {
            mqtt_start();
        }
        
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

// Obter configuração atual
esp_err_t mqtt_get_config(mqtt_config_t *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(mqtt_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        memcpy(config, &mqtt_config, sizeof(mqtt_config_t));
        xSemaphoreGive(mqtt_mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}