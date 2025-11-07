/**
 * @file modbus_tcp_slave.c
 * @brief Implementação da biblioteca Modbus TCP Slave
 */

#include "modbus_tcp_slave.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "string.h"

// Mapear diretamente os registradores globais usados pelo RTU
#include "modbus_params.h"

// Incluir os headers do FreeModbus
#include "mbcontroller.h"
#include "esp_modbus_common.h"
#include "esp_modbus_slave.h"

static const char *TAG = "MODBUS_TCP_SLAVE";

// Estrutura interna da instância
typedef struct {
    modbus_tcp_config_t config;
    modbus_tcp_callbacks_t callbacks;
    modbus_tcp_state_t state;
    
    // Registros Modbus
    modbus_holding_regs_t holding_regs;
    modbus_input_regs_t input_regs;
    modbus_coil_regs_t coil_regs;
    modbus_discrete_regs_t discrete_regs;
    
    // Controle de acesso
    SemaphoreHandle_t mutex;
    TaskHandle_t operation_task;
    
    // Info de conexão
    uint8_t connection_count;
    bool is_running;
    
} modbus_tcp_instance_t;

// Defines para compatibilidade com o código original
#define HOLD_OFFSET(field) ((uint16_t)(offsetof(modbus_holding_regs_t, field) >> 1))
#define INPUT_OFFSET(field) ((uint16_t)(offsetof(modbus_input_regs_t, field) >> 1))

#define MB_REG_DISCRETE_INPUT_START         (0x0000)
#define MB_REG_COILS_START                  (0x0000)
#define MB_REG_INPUT_START_AREA0            (INPUT_OFFSET(input_data0))
#define MB_REG_INPUT_START_AREA1            (INPUT_OFFSET(input_data4))
#define MB_REG_HOLDING_START_AREA0          (HOLD_OFFSET(holding_data0))
#define MB_REG_HOLDING_START_AREA1          (HOLD_OFFSET(holding_data4))

#define MB_PAR_INFO_GET_TOUT                (10)
#define MB_CHAN_DATA_MAX_VAL                (10)
#define MB_CHAN_DATA_OFFSET                 (1.1f)

#define MB_READ_MASK                        (MB_EVENT_INPUT_REG_RD \
                                                | MB_EVENT_HOLDING_REG_RD \
                                                | MB_EVENT_DISCRETE_RD \
                                                | MB_EVENT_COILS_RD)
#define MB_WRITE_MASK                       (MB_EVENT_HOLDING_REG_WR \
                                                | MB_EVENT_COILS_WR)
#define MB_READ_WRITE_MASK                  (MB_READ_MASK | MB_WRITE_MASK)

// Função para validar handle
static bool is_valid_handle(modbus_tcp_handle_t handle) {
    return (handle != NULL);
}

// Função para obter instância
static modbus_tcp_instance_t* get_instance(modbus_tcp_handle_t handle) {
    if (!is_valid_handle(handle)) {
        return NULL;
    }
    return (modbus_tcp_instance_t*)handle;
}

// Configuração inicial dos registros
static void setup_reg_data(modbus_tcp_instance_t *instance) {
    // Discrete Inputs (mantidos locais - não há equivalentes globais definidos)
    instance->discrete_regs.discrete_input0 = 1;
    instance->discrete_regs.discrete_input1 = 0;
    instance->discrete_regs.discrete_input2 = 1;
    instance->discrete_regs.discrete_input3 = 0;
    instance->discrete_regs.discrete_input4 = 1;
    instance->discrete_regs.discrete_input5 = 0;
    instance->discrete_regs.discrete_input6 = 1;
    instance->discrete_regs.discrete_input7 = 0;

    // Holding Registers base (0-7) - usar memória global compartilhada
    holding_reg_params.holding_data0 = 1.34f;
    holding_reg_params.holding_data1 = 2.56f;
    holding_reg_params.holding_data2 = 3.78f;
    holding_reg_params.holding_data3 = 4.90f;
    holding_reg_params.holding_data4 = 5.67f;
    holding_reg_params.holding_data5 = 6.78f;
    holding_reg_params.holding_data6 = 7.79f;
    holding_reg_params.holding_data7 = 8.80f;

    // Coils - usar memória global compartilhada
    coil_reg_params.coils_port0 = 0x55;
    coil_reg_params.coils_port1 = 0xAA;

    // Input Registers base (0-7) - usar memória global compartilhada
    input_reg_params.input_data0 = 1.12f;
    input_reg_params.input_data1 = 2.34f;
    input_reg_params.input_data2 = 3.56f;
    input_reg_params.input_data3 = 4.78f;
    input_reg_params.input_data4 = 1.12f;
    input_reg_params.input_data5 = 2.34f;
    input_reg_params.input_data6 = 3.56f;
    input_reg_params.input_data7 = 4.78f;
}

// Task de operação do Modbus (baseada no código original)
void slave_operation_task(void *arg) {
    modbus_tcp_instance_t *instance = (modbus_tcp_instance_t*)arg;
    mb_param_info_t reg_info;

    ESP_LOGI(TAG, "Modbus slave operation task started");

    while (instance->is_running) {
        // Check for read/write events
        esp_err_t err = mbc_slave_check_event(MB_READ_WRITE_MASK);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "mbc_slave_check_event returned error: %s", esp_err_to_name(err));
        } else {
            err = mbc_slave_get_param_info(&reg_info, MB_PAR_INFO_GET_TOUT);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "mbc_slave_get_param_info failed: %s", esp_err_to_name(err));
            } else {
                const char* rw_str = (reg_info.type & MB_READ_MASK) ? "READ" : "WRITE";

                // Log a decoded summary for easier debugging
                ESP_LOGD(TAG, "mbc event: type=0x%04x offset=%u size=%u", (unsigned)reg_info.type,
                         (unsigned)reg_info.mb_offset, (unsigned)reg_info.size);

                // Process events and call callbacks (if registered) with extra debug
                if (reg_info.type & (MB_EVENT_HOLDING_REG_WR | MB_EVENT_HOLDING_REG_RD)) {
                    ESP_LOGI(TAG, "HOLDING %s addr=%u size=%u", rw_str, (unsigned)reg_info.mb_offset, (unsigned)reg_info.size);
                    if (instance->callbacks.on_register_read && (reg_info.type & MB_READ_MASK)) {
                        ESP_LOGD(TAG, "Invoking on_register_read for HOLDING @%u", (unsigned)reg_info.mb_offset);
                        instance->callbacks.on_register_read(reg_info.mb_offset, MODBUS_REG_HOLDING, 0);
                    }
                    if (instance->callbacks.on_register_write && (reg_info.type & MB_WRITE_MASK)) {
                        ESP_LOGD(TAG, "Invoking on_register_write for HOLDING @%u", (unsigned)reg_info.mb_offset);
                        instance->callbacks.on_register_write(reg_info.mb_offset, MODBUS_REG_HOLDING, 0);
                    }

                } else if (reg_info.type & MB_EVENT_INPUT_REG_RD) {
                    ESP_LOGI(TAG, "INPUT READ addr=%u size=%u", (unsigned)reg_info.mb_offset, (unsigned)reg_info.size);
                    if (instance->callbacks.on_register_read) {
                        ESP_LOGD(TAG, "Invoking on_register_read for INPUT @%u", (unsigned)reg_info.mb_offset);
                        instance->callbacks.on_register_read(reg_info.mb_offset, MODBUS_REG_INPUT, 0);
                    }

                } else if (reg_info.type & MB_EVENT_DISCRETE_RD) {
                    ESP_LOGI(TAG, "DISCRETE READ addr=%u size=%u", (unsigned)reg_info.mb_offset, (unsigned)reg_info.size);
                    if (instance->callbacks.on_register_read) {
                        ESP_LOGD(TAG, "Invoking on_register_read for DISCRETE @%u", (unsigned)reg_info.mb_offset);
                        instance->callbacks.on_register_read(reg_info.mb_offset, MODBUS_REG_DISCRETE, 0);
                    }

                } else if (reg_info.type & (MB_EVENT_COILS_RD | MB_EVENT_COILS_WR)) {
                    ESP_LOGI(TAG, "COILS %s addr=%u size=%u", rw_str, (unsigned)reg_info.mb_offset, (unsigned)reg_info.size);
                    if (instance->callbacks.on_register_read && (reg_info.type & MB_READ_MASK)) {
                        ESP_LOGD(TAG, "Invoking on_register_read for COIL @%u", (unsigned)reg_info.mb_offset);
                        instance->callbacks.on_register_read(reg_info.mb_offset, MODBUS_REG_COIL, 0);
                    }
                    if (instance->callbacks.on_register_write && (reg_info.type & MB_WRITE_MASK)) {
                        ESP_LOGD(TAG, "Invoking on_register_write for COIL @%u", (unsigned)reg_info.mb_offset);
                        instance->callbacks.on_register_write(reg_info.mb_offset, MODBUS_REG_COIL, 0);
                    }
                } else {
                    ESP_LOGW(TAG, "Unhandled mbc event type: 0x%04x", (unsigned)reg_info.type);
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10)); // Small delay
    }

    ESP_LOGI(TAG, "Modbus slave operation task ended");
    vTaskDelete(NULL);
}

// ================================
// IMPLEMENTAÇÃO DA API PÚBLICA
// ================================

esp_err_t modbus_tcp_slave_init(const modbus_tcp_config_t *config, modbus_tcp_handle_t *handle) {
    if (!config || !handle) {
        return ESP_ERR_INVALID_ARG;
    }

    // Alocar instância
    modbus_tcp_instance_t *instance = malloc(sizeof(modbus_tcp_instance_t));
    if (!instance) {
        return ESP_ERR_NO_MEM;
    }

    // Zerar a estrutura
    memset(instance, 0, sizeof(modbus_tcp_instance_t));

    // Copiar configuração
    memcpy(&instance->config, config, sizeof(modbus_tcp_config_t));

    // Configurações padrão
    if (instance->config.port == 0) {
        instance->config.port = 502;
    }
    if (instance->config.slave_id == 0) {
        instance->config.slave_id = 1;
    }
    if (instance->config.max_connections == 0) {
        instance->config.max_connections = 5;
    }
    if (instance->config.timeout_ms == 0) {
        instance->config.timeout_ms = 20000;
    }

    // Criar mutex
    instance->mutex = xSemaphoreCreateMutex();
    if (!instance->mutex) {
        free(instance);
        return ESP_ERR_NO_MEM;
    }

    // Estado inicial
    instance->state = MODBUS_TCP_STATE_STOPPED;
    
    // Configurar registros iniciais
    setup_reg_data(instance);

    *handle = instance;

    ESP_LOGI(TAG, "Modbus TCP Slave initialized - Port: %d, Slave ID: %d", 
             instance->config.port, instance->config.slave_id);

    // Auto start se configurado
    if (instance->config.auto_start) {
        return modbus_tcp_slave_start(*handle);
    }

    return ESP_OK;
}

esp_err_t modbus_tcp_slave_start(modbus_tcp_handle_t handle) {
    modbus_tcp_instance_t *instance = get_instance(handle);
    if (!instance) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(instance->mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (instance->state != MODBUS_TCP_STATE_STOPPED) {
        xSemaphoreGive(instance->mutex);
        return ESP_ERR_INVALID_STATE;
    }

    instance->state = MODBUS_TCP_STATE_STARTING;

    // Declarar variável para capturar erros
    esp_err_t err;

    // Inicializar controlador Modbus TCP (usa o port handler do componente TCP)
    // Chama mbc_slave_init_tcp para registrar a interface interna do slave
    ESP_LOGI(TAG, "Initializing Modbus TCP slave controller interface...");
    void *port_handler = NULL;
    err = mbc_slave_init_tcp(&port_handler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mbc_slave_init_tcp failed: %s", esp_err_to_name(err));
        instance->state = MODBUS_TCP_STATE_ERROR;
        xSemaphoreGive(instance->mutex);
        return err;
    }
    ESP_LOGI(TAG, "Modbus TCP slave controller initialized internally");

    // Configurar comunicação
    mb_communication_info_t comm_info = {0};
    comm_info.ip_addr = NULL; // Bind to any address
    comm_info.ip_netif_ptr = (void*)instance->config.netif;
    comm_info.slave_uid = instance->config.slave_id;
    comm_info.ip_port = instance->config.port;
    
#if !CONFIG_EXAMPLE_CONNECT_IPV6
    comm_info.ip_addr_type = MB_IPV4;
#else
    comm_info.ip_addr_type = MB_IPV6;
#endif
    comm_info.ip_mode = MB_MODE_TCP;

    // Setup e start
    err = mbc_slave_setup((void*)&comm_info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to setup Modbus slave: %s", esp_err_to_name(err));
        instance->state = MODBUS_TCP_STATE_ERROR;
        xSemaphoreGive(instance->mutex);
        return err;
    }

    // Configurar áreas de registros
    mb_register_area_descriptor_t reg_area;

    // Holding Registers - Area 0 (usar memória global do RTU)
    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = MB_REG_HOLDING_START_AREA0;
    reg_area.address = (void*)&holding_reg_params.holding_data0;
    reg_area.size = (MB_REG_HOLDING_START_AREA1 - MB_REG_HOLDING_START_AREA0) << 1;
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set holding registers area 0: %s", esp_err_to_name(err));
        instance->state = MODBUS_TCP_STATE_ERROR;
        xSemaphoreGive(instance->mutex);
        return err;
    }

    // Holding Registers - Area 1 (usar memória global do RTU)
    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = MB_REG_HOLDING_START_AREA1;
    reg_area.address = (void*)&holding_reg_params.holding_data4;
    reg_area.size = sizeof(float) << 2;
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set holding registers area 1: %s", esp_err_to_name(err));
        instance->state = MODBUS_TCP_STATE_ERROR;
        xSemaphoreGive(instance->mutex);
        return err;
    }

    // Input Registers - Area 0 (usar memória global do RTU)
    reg_area.type = MB_PARAM_INPUT;
    reg_area.start_offset = MB_REG_INPUT_START_AREA0;
    reg_area.address = (void*)&input_reg_params.input_data0;
    reg_area.size = sizeof(float) << 2;
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set input registers area 0: %s", esp_err_to_name(err));
        instance->state = MODBUS_TCP_STATE_ERROR;
        xSemaphoreGive(instance->mutex);
        return err;
    }

    // Input Registers - Area 1 (usar memória global do RTU)
    reg_area.type = MB_PARAM_INPUT;
    reg_area.start_offset = MB_REG_INPUT_START_AREA1;
    reg_area.address = (void*)&input_reg_params.input_data4;
    reg_area.size = sizeof(float) << 2;
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set input registers area 1: %s", esp_err_to_name(err));
        instance->state = MODBUS_TCP_STATE_ERROR;
        xSemaphoreGive(instance->mutex);
        return err;
    }

    // Coils (usar memória global do RTU)
    reg_area.type = MB_PARAM_COIL;
    reg_area.start_offset = MB_REG_COILS_START;
    reg_area.address = (void*)&coil_reg_params;
    reg_area.size = sizeof(coil_reg_params_t);
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set coils: %s", esp_err_to_name(err));
        instance->state = MODBUS_TCP_STATE_ERROR;
        xSemaphoreGive(instance->mutex);
        return err;
    }

    // Discrete Inputs
    reg_area.type = MB_PARAM_DISCRETE;
    reg_area.start_offset = MB_REG_DISCRETE_INPUT_START;
    reg_area.address = (void*)&instance->discrete_regs;
    reg_area.size = sizeof(modbus_discrete_regs_t);
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set discrete inputs: %s", esp_err_to_name(err));
        instance->state = MODBUS_TCP_STATE_ERROR;
        xSemaphoreGive(instance->mutex);
        return err;
    }

    // Registrar áreas adicionais de Holding Registers
    // REG 1000 - Configuração (3 registros) -> usa memória global compartilhada
    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = REG_CONFIG_START;
    reg_area.address = (void*)holding_reg1000_params.reg1000;
    reg_area.size = REG_CONFIG_SIZE * sizeof(uint16_t);
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set holding registers 1000: %s", esp_err_to_name(err));
        instance->state = MODBUS_TCP_STATE_ERROR;
        xSemaphoreGive(instance->mutex);
        return err;
    }

    // REG 2000 - Data (1 registro) -> global reg2000
    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = REG_DATA_START;
    reg_area.address = (void*)reg2000;
    reg_area.size = REG_DATA_SIZE * sizeof(uint16_t);
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set holding registers 2000: %s", esp_err_to_name(err));
        instance->state = MODBUS_TCP_STATE_ERROR;
        xSemaphoreGive(instance->mutex);
        return err;
    }

    // REG 3000 -> global reg3000
    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = REG_3000_START;
    reg_area.address = (void*)reg3000;
    reg_area.size = REG_3000_SIZE * sizeof(uint16_t);
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set holding registers 3000: %s", esp_err_to_name(err));
        instance->state = MODBUS_TCP_STATE_ERROR;
        xSemaphoreGive(instance->mutex);
        return err;
    }

    // REG 4000 -> global reg4000
    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = REG_4000_START;
    reg_area.address = (void*)reg4000;
    reg_area.size = REG_4000_SIZE * sizeof(uint16_t);
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set holding registers 4000: %s", esp_err_to_name(err));
        instance->state = MODBUS_TCP_STATE_ERROR;
        xSemaphoreGive(instance->mutex);
        return err;
    }

    // REG 5000 -> global reg5000
    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = REG_5000_START;
    reg_area.address = (void*)reg5000;
    reg_area.size = REG_5000_SIZE * sizeof(uint16_t);
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set holding registers 5000: %s", esp_err_to_name(err));
        instance->state = MODBUS_TCP_STATE_ERROR;
        xSemaphoreGive(instance->mutex);
        return err;
    }

    // REG 6000 -> global reg6000
    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = REG_6000_START;
    reg_area.address = (void*)reg6000;
    reg_area.size = REG_6000_SIZE * sizeof(uint16_t);
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set holding registers 6000: %s", esp_err_to_name(err));
        instance->state = MODBUS_TCP_STATE_ERROR;
        xSemaphoreGive(instance->mutex);
        return err;
    }

    // REG 7000 -> global reg7000
    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = REG_7000_START;
    reg_area.address = (void*)reg7000;
    reg_area.size = REG_7000_SIZE * sizeof(uint16_t);
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set holding registers 7000: %s", esp_err_to_name(err));
        instance->state = MODBUS_TCP_STATE_ERROR;
        xSemaphoreGive(instance->mutex);
        return err;
    }

    // REG 8000 -> global reg8000
    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = REG_8000_START;
    reg_area.address = (void*)reg8000;
    reg_area.size = REG_8000_SIZE * sizeof(uint16_t);
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set holding registers 8000: %s", esp_err_to_name(err));
        instance->state = MODBUS_TCP_STATE_ERROR;
        xSemaphoreGive(instance->mutex);
        return err;
    }

    // REG 9000 - Unit Specs -> global reg9000
    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = REG_UNITSPECS_START;
    reg_area.address = (void*)reg9000;
    reg_area.size = REG_UNITSPECS_SIZE * sizeof(uint16_t);
    err = mbc_slave_set_descriptor(reg_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set holding registers 9000: %s", esp_err_to_name(err));
        instance->state = MODBUS_TCP_STATE_ERROR;
        xSemaphoreGive(instance->mutex);
        return err;
    }

    // Start Modbus slave
    err = mbc_slave_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Modbus slave: %s", esp_err_to_name(err));
        instance->state = MODBUS_TCP_STATE_ERROR;
        xSemaphoreGive(instance->mutex);
        return err;
    }

    // Criar task de operação
    instance->is_running = true;
    BaseType_t task_created = xTaskCreate(slave_operation_task, 
                                         "modbus_tcp_operation", 
                                         4096, 
                                         instance, 
                                         5, 
                                         &instance->operation_task);
    if (task_created != pdTRUE) {
        ESP_LOGE(TAG, "Failed to create operation task");
        instance->is_running = false;
        mbc_slave_destroy();
        instance->state = MODBUS_TCP_STATE_ERROR;
        xSemaphoreGive(instance->mutex);
        return ESP_ERR_NO_MEM;
    }

    instance->state = MODBUS_TCP_STATE_RUNNING;
    
    xSemaphoreGive(instance->mutex);

    ESP_LOGI(TAG, "Modbus TCP Slave started successfully on port %d", instance->config.port);

    return ESP_OK;
}

esp_err_t modbus_tcp_slave_stop(modbus_tcp_handle_t handle) {
    modbus_tcp_instance_t *instance = get_instance(handle);
    if (!instance) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(instance->mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (instance->state != MODBUS_TCP_STATE_RUNNING) {
        xSemaphoreGive(instance->mutex);
        return ESP_ERR_INVALID_STATE;
    }

    instance->state = MODBUS_TCP_STATE_STOPPING;

    // Parar task de operação
    instance->is_running = false;
    if (instance->operation_task) {
        // Aguardar task terminar
        vTaskDelay(pdMS_TO_TICKS(100));
        instance->operation_task = NULL;
    }

    // Destruir controlador Modbus
    esp_err_t err = mbc_slave_destroy();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Warning during Modbus destroy: %s", esp_err_to_name(err));
    }

    instance->state = MODBUS_TCP_STATE_STOPPED;
    
    xSemaphoreGive(instance->mutex);

    ESP_LOGI(TAG, "Modbus TCP Slave stopped");

    return ESP_OK;
}

esp_err_t modbus_tcp_slave_destroy(modbus_tcp_handle_t handle) {
    modbus_tcp_instance_t *instance = get_instance(handle);
    if (!instance) {
        return ESP_ERR_INVALID_ARG;
    }

    // Parar se estiver rodando
    if (instance->state == MODBUS_TCP_STATE_RUNNING) {
        modbus_tcp_slave_stop(handle);
    }

    // Destruir mutex
    if (instance->mutex) {
        vSemaphoreDelete(instance->mutex);
    }

    // Liberar memória
    free(instance);

    ESP_LOGI(TAG, "Modbus TCP Slave destroyed");

    return ESP_OK;
}

modbus_tcp_state_t modbus_tcp_slave_get_state(modbus_tcp_handle_t handle) {
    modbus_tcp_instance_t *instance = get_instance(handle);
    if (!instance) {
        return MODBUS_TCP_STATE_ERROR;
    }
    return instance->state;
}

// ================================
// FUNÇÕES DE REGISTRO
// ================================

esp_err_t modbus_tcp_set_holding_reg_float(modbus_tcp_handle_t handle, uint16_t addr, float value) {
    modbus_tcp_instance_t *instance = get_instance(handle);
    if (!instance) {
        return ESP_ERR_INVALID_ARG;
    }

    // Endereços básicos (0-7) armazenados como float - usar memória global compartilhada
    if (addr <= 7) {
        if (xSemaphoreTake(instance->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            return ESP_ERR_TIMEOUT;
        }
        float *reg_ptr = (float*)&holding_reg_params;
        reg_ptr[addr] = value;
        xSemaphoreGive(instance->mutex);
        return ESP_OK;
    }

    // Endereços estendidos - escrever diretamente nos arrays globais (uint16_t)
    uint16_t as_u16 = (uint16_t)value;
    if (addr >= REG_CONFIG_START && addr < REG_CONFIG_START + REG_CONFIG_SIZE) {
        holding_reg1000_params.reg1000[addr - REG_CONFIG_START] = as_u16;
        return ESP_OK;
    } else if (addr >= REG_DATA_START && addr < REG_DATA_START + REG_DATA_SIZE) {
        reg2000[addr - REG_DATA_START] = as_u16;
        return ESP_OK;
    } else if (addr >= REG_3000_START && addr < REG_3000_START + REG_3000_SIZE) {
        reg3000[addr - REG_3000_START] = as_u16;
        return ESP_OK;
    } else if (addr >= REG_4000_START && addr < REG_4000_START + REG_4000_SIZE) {
        reg4000[addr - REG_4000_START] = as_u16;
        return ESP_OK;
    } else if (addr >= REG_5000_START && addr < REG_5000_START + REG_5000_SIZE) {
        reg5000[addr - REG_5000_START] = as_u16;
        return ESP_OK;
    } else if (addr >= REG_6000_START && addr < REG_6000_START + REG_6000_SIZE) {
        reg6000[addr - REG_6000_START] = as_u16;
        return ESP_OK;
    } else if (addr >= REG_7000_START && addr < REG_7000_START + REG_7000_SIZE) {
        reg7000[addr - REG_7000_START] = as_u16;
        return ESP_OK;
    } else if (addr >= REG_8000_START && addr < REG_8000_START + REG_8000_SIZE) {
        reg8000[addr - REG_8000_START] = as_u16;
        return ESP_OK;
    } else if (addr >= REG_UNITSPECS_START && addr < REG_UNITSPECS_START + REG_UNITSPECS_SIZE) {
        reg9000[addr - REG_UNITSPECS_START] = as_u16;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_ARG;
}

esp_err_t modbus_tcp_get_holding_reg_float(modbus_tcp_handle_t handle, uint16_t addr, float *value) {
    modbus_tcp_instance_t *instance = get_instance(handle);
    if (!instance || !value) {
        return ESP_ERR_INVALID_ARG;
    }

    if (addr <= 7) {
        if (xSemaphoreTake(instance->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            return ESP_ERR_TIMEOUT;
        }
        float *reg_ptr = (float*)&holding_reg_params;
        *value = reg_ptr[addr];
        xSemaphoreGive(instance->mutex);
        return ESP_OK;
    }

    if (addr >= REG_CONFIG_START && addr < REG_CONFIG_START + REG_CONFIG_SIZE) {
        *value = (float)holding_reg1000_params.reg1000[addr - REG_CONFIG_START];
        return ESP_OK;
    } else if (addr >= REG_DATA_START && addr < REG_DATA_START + REG_DATA_SIZE) {
        *value = (float)reg2000[addr - REG_DATA_START];
        return ESP_OK;
    } else if (addr >= REG_3000_START && addr < REG_3000_START + REG_3000_SIZE) {
        *value = (float)reg3000[addr - REG_3000_START];
        return ESP_OK;
    } else if (addr >= REG_4000_START && addr < REG_4000_START + REG_4000_SIZE) {
        *value = (float)reg4000[addr - REG_4000_START];
        return ESP_OK;
    } else if (addr >= REG_5000_START && addr < REG_5000_START + REG_5000_SIZE) {
        *value = (float)reg5000[addr - REG_5000_START];
        return ESP_OK;
    } else if (addr >= REG_6000_START && addr < REG_6000_START + REG_6000_SIZE) {
        *value = (float)reg6000[addr - REG_6000_START];
        return ESP_OK;
    } else if (addr >= REG_7000_START && addr < REG_7000_START + REG_7000_SIZE) {
        *value = (float)reg7000[addr - REG_7000_START];
        return ESP_OK;
    } else if (addr >= REG_8000_START && addr < REG_8000_START + REG_8000_SIZE) {
        *value = (float)reg8000[addr - REG_8000_START];
        return ESP_OK;
    } else if (addr >= REG_UNITSPECS_START && addr < REG_UNITSPECS_START + REG_UNITSPECS_SIZE) {
        *value = (float)reg9000[addr - REG_UNITSPECS_START];
        return ESP_OK;
    }
    return ESP_ERR_INVALID_ARG;
}

esp_err_t modbus_tcp_set_input_reg_float(modbus_tcp_handle_t handle, uint16_t addr, float value) {
    modbus_tcp_instance_t *instance = get_instance(handle);
    if (!instance || addr > 7) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(instance->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    float *reg_ptr = (float*)&input_reg_params;
    reg_ptr[addr] = value;

    xSemaphoreGive(instance->mutex);
    return ESP_OK;
}

esp_err_t modbus_tcp_get_input_reg_float(modbus_tcp_handle_t handle, uint16_t addr, float *value) {
    modbus_tcp_instance_t *instance = get_instance(handle);
    if (!instance || !value || addr > 7) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(instance->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    float *reg_ptr = (float*)&input_reg_params;
    *value = reg_ptr[addr];

    xSemaphoreGive(instance->mutex);
    return ESP_OK;
}

esp_err_t modbus_tcp_set_coil(modbus_tcp_handle_t handle, uint16_t addr, bool value) {
    modbus_tcp_instance_t *instance = get_instance(handle);
    if (!instance || addr > 15) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(instance->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    uint8_t *coil_ptr = (uint8_t*)&coil_reg_params;
    uint8_t byte_idx = addr / 8;
    uint8_t bit_idx = addr % 8;
    
    if (value) {
        coil_ptr[byte_idx] |= (1 << bit_idx);
    } else {
        coil_ptr[byte_idx] &= ~(1 << bit_idx);
    }

    xSemaphoreGive(instance->mutex);
    return ESP_OK;
}

esp_err_t modbus_tcp_get_coil(modbus_tcp_handle_t handle, uint16_t addr, bool *value) {
    modbus_tcp_instance_t *instance = get_instance(handle);
    if (!instance || !value || addr > 15) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(instance->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    uint8_t *coil_ptr = (uint8_t*)&coil_reg_params;
    uint8_t byte_idx = addr / 8;
    uint8_t bit_idx = addr % 8;
    
    *value = (coil_ptr[byte_idx] & (1 << bit_idx)) != 0;

    xSemaphoreGive(instance->mutex);
    return ESP_OK;
}

esp_err_t modbus_tcp_set_discrete_input(modbus_tcp_handle_t handle, uint16_t addr, bool value) {
    modbus_tcp_instance_t *instance = get_instance(handle);
    if (!instance || addr > 7) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(instance->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    uint8_t *discrete_ptr = (uint8_t*)&instance->discrete_regs;
    if (value) {
        *discrete_ptr |= (1 << addr);
    } else {
        *discrete_ptr &= ~(1 << addr);
    }

    xSemaphoreGive(instance->mutex);
    return ESP_OK;
}

esp_err_t modbus_tcp_get_discrete_input(modbus_tcp_handle_t handle, uint16_t addr, bool *value) {
    modbus_tcp_instance_t *instance = get_instance(handle);
    if (!instance || !value || addr > 7) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(instance->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    uint8_t *discrete_ptr = (uint8_t*)&instance->discrete_regs;
    *value = (*discrete_ptr & (1 << addr)) != 0;

    xSemaphoreGive(instance->mutex);
    return ESP_OK;
}

esp_err_t modbus_tcp_register_callbacks(modbus_tcp_handle_t handle, const modbus_tcp_callbacks_t *callbacks) {
    modbus_tcp_instance_t *instance = get_instance(handle);
    if (!instance || !callbacks) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(instance->mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memcpy(&instance->callbacks, callbacks, sizeof(modbus_tcp_callbacks_t));

    xSemaphoreGive(instance->mutex);

    ESP_LOGI(TAG, "Callbacks registered");
    return ESP_OK;
}

esp_err_t modbus_tcp_get_registers_ptr(modbus_tcp_handle_t handle,
                                       modbus_holding_regs_t **holding,
                                       modbus_input_regs_t **input,
                                       modbus_coil_regs_t **coils,
                                       modbus_discrete_regs_t **discrete) {
    modbus_tcp_instance_t *instance = get_instance(handle);
    if (!instance) {
        return ESP_ERR_INVALID_ARG;
    }

    if (holding) *holding = &instance->holding_regs;
    if (input) *input = &instance->input_regs;
    if (coils) *coils = &instance->coil_regs;
    if (discrete) *discrete = &instance->discrete_regs;

    return ESP_OK;
}

esp_err_t modbus_tcp_get_connection_info(modbus_tcp_handle_t handle, uint8_t *connection_count, uint16_t *port) {
    modbus_tcp_instance_t *instance = get_instance(handle);
    if (!instance) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (connection_count) *connection_count = instance->connection_count;
    if (port) *port = instance->config.port;
    
    return ESP_OK;
}

// ================================
// FUNÇÕES DE COMPATIBILIDADE RTU (uint16_t)
// ================================

esp_err_t modbus_tcp_set_holding_register(modbus_tcp_handle_t handle, uint16_t addr, uint16_t value) {
    modbus_tcp_instance_t *instance = get_instance(handle);
    if (!instance) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Converte uint16_t para float e usa a função existente
    float float_value = (float)value;
    return modbus_tcp_set_holding_reg_float(handle, addr, float_value);
}

esp_err_t modbus_tcp_get_holding_register(modbus_tcp_handle_t handle, uint16_t addr, uint16_t *value) {
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    float float_value;
    esp_err_t result = modbus_tcp_get_holding_reg_float(handle, addr, &float_value);
    
    if (result == ESP_OK) {
        // Converte float para uint16_t
        *value = (uint16_t)float_value;
    }
    
    return result;
}

esp_err_t modbus_tcp_set_input_register(modbus_tcp_handle_t handle, uint16_t addr, uint16_t value) {
    modbus_tcp_instance_t *instance = get_instance(handle);
    if (!instance) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Converte uint16_t para float e usa a função existente
    float float_value = (float)value;
    return modbus_tcp_set_input_reg_float(handle, addr, float_value);
}

esp_err_t modbus_tcp_get_input_register(modbus_tcp_handle_t handle, uint16_t addr, uint16_t *value) {
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    float float_value;
    esp_err_t result = modbus_tcp_get_input_reg_float(handle, addr, &float_value);
    
    if (result == ESP_OK) {
        // Converte float para uint16_t
        *value = (uint16_t)float_value;
    }
    
    return result;
}