#ifndef MQTT_CLIENT_TASK_H
#define MQTT_CLIENT_TASK_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Configurações MQTT
#define MQTT_BROKER_URL         "mqtt://broker.hivemq.com"  // HiveMQ público
#define MQTT_BROKER_WEBSOCKET   "ws://broker.hivemq.com:8000/mqtt"  // WebSocket
#define MQTT_PORT               1883
#define MQTT_CLIENT_ID          "ESP32_SondaLambda"
#define MQTT_KEEPALIVE          60
#define MQTT_QOS_LEVEL          1
#define MQTT_RETAIN             false

// Tópicos MQTT
#define MQTT_TOPIC_BASE         "esp32/sonda_lambda"
#define MQTT_TOPIC_HEAT         MQTT_TOPIC_BASE "/heat"
#define MQTT_TOPIC_LAMBDA       MQTT_TOPIC_BASE "/lambda"
#define MQTT_TOPIC_O2           MQTT_TOPIC_BASE "/o2"
#define MQTT_TOPIC_ERROR        MQTT_TOPIC_BASE "/error"
#define MQTT_TOPIC_OUTPUT       MQTT_TOPIC_BASE "/output"
#define MQTT_TOPIC_STATUS       MQTT_TOPIC_BASE "/status"
#define MQTT_TOPIC_ALL_DATA     MQTT_TOPIC_BASE "/data"

// Estrutura para dados da sonda
typedef struct {
    int16_t heat_value;
    int16_t lambda_value;
    int16_t error_value;
    uint16_t o2_percent;
    uint32_t output_value;
    uint32_t timestamp_ms;
    bool valid;
} sonda_data_t;

// Incluir estrutura MQTT do config_manager
#include "config_manager.h"

// Enums para status MQTT
typedef enum {
    MQTT_STATE_DISCONNECTED = 0,
    MQTT_STATE_CONNECTING,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_ERROR
} mqtt_state_t;

// Funções principais
void mqtt_client_task(void *pvParameters);
esp_err_t mqtt_init(void);
esp_err_t mqtt_publish_sonda_data(const sonda_data_t *data);
esp_err_t mqtt_publish_individual_values(int16_t heat, int16_t lambda, int16_t error, uint16_t o2, uint32_t output);
esp_err_t mqtt_set_config(const mqtt_config_t *config);
esp_err_t mqtt_get_config(mqtt_config_t *config);
mqtt_state_t mqtt_get_state(void);
bool mqtt_is_connected(void);

// Funções de controle
esp_err_t mqtt_start(void);
esp_err_t mqtt_stop(void);
esp_err_t mqtt_restart(void);

// Funções de callback (para integração com outras tasks)
typedef void (*mqtt_data_callback_t)(const sonda_data_t *data);
esp_err_t mqtt_set_data_callback(mqtt_data_callback_t callback);

// Função para enviar dados de outras tasks para o MQTT
esp_err_t mqtt_send_data_to_queue(int16_t heat, int16_t lambda, int16_t error, uint16_t o2, uint32_t output);

#endif // MQTT_CLIENT_TASK_H