#include "filas.h"

static const char *TAG = "GERENCIADOR FILAS";

QueueHandle_t filaDadosSonda = NULL;

// inicializa a fila para dados da sonda
esp_err_t initFilaSonda(){
    // Cria a fila para dados da sonda
    filaDadosSonda = xQueueCreate(TAM_FILA_SONDA, sizeof(sonda_data_t));
    // Valida se a fila foi criada com sucesso
    if (filaDadosSonda == NULL) {
        ESP_LOGE(TAG, "ERRO: Falha ao criar fila de dados da sonda.");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Fila de dados da sonda criada com sucesso.");
    return ESP_OK;
}

// preenche a estrutura de dados da sonda
sonda_data_t preencheDadosSonda(uint16_t heatValue, int16_t lambdaValue, uint16_t heatRef, uint16_t lambdaRef, uint16_t o2Percent, uint32_t controlValue) {
    // cria a estrutura de dados da sonda
    sonda_data_t dadosSonda;
    // preenche os campos da estrutura
    dadosSonda.heatValue = heatValue;
    dadosSonda.lambdaValue = lambdaValue;
    dadosSonda.heatRef = heatRef;
    dadosSonda.lambdaRef = lambdaRef;
    dadosSonda.o2Percent = o2Percent;
    dadosSonda.controlValue = controlValue;
    return dadosSonda;
}

// Escreve dados na fila da Sonda
esp_err_t inserirFilaSonda(uint16_t heatValue, int16_t lambdaValue, uint16_t heatRef, uint16_t lambdaRef, uint16_t o2Percent, uint32_t controlValue) {
    // Monta a estrutura de dados da sonda
    sonda_data_t dadosSonda =  preencheDadosSonda(heatValue, lambdaValue, heatRef, lambdaRef, o2Percent, controlValue);
    
    // Verifica o número de elementos pendentes na fila
    uint32_t pendenciasFila = elementosFilaPendentes();
    ESP_LOGI(TAG, "Elementos pendentes na fila: %d", pendenciasFila);
    
    // Envia os dados para a fila
    BaseType_t resultado = xQueueSend(filaDadosSonda, &dadosSonda, pdMS_TO_TICKS(TEMPO_ESPERA_FILA));
    
    if (resultado == pdTRUE) {
        ESP_LOGI(TAG, "Dados da sonda enviados para a fila com sucesso.");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "ERRO: Falha ao enviar dados para a fila de dados da sonda.");
        return ESP_ERR_TIMEOUT;
    }
}


uint32_t elementosFilaPendentes(void) {
    if (filaDadosSonda == NULL) {
        return 0;
    }

    UBaseType_t count = uxQueueMessagesWaiting(filaDadosSonda);
    return (uint32_t)count;
}


// Lê a estrutura de dados da sonda
esp_err_t removerFilaSonda(uint16_t *heatValue, int16_t *lambdaValue, uint16_t *heatRef, uint16_t *lambdaRef, uint16_t *o2Percent, uint32_t *controlValue) {
    // estrutura para armazenar os dados lidos da fila
    sonda_data_t dadosSonda;
    
    // Retira os dados da fila da sonda
    BaseType_t resultado = xQueueReceive(filaDadosSonda, &dadosSonda, pdMS_TO_TICKS(TEMPO_ESPERA_FILA));

    if (resultado == pdTRUE) {
        *heatValue = dadosSonda.heatValue;
        *lambdaValue = dadosSonda.lambdaValue;
        *heatRef = dadosSonda.heatRef;
        *lambdaRef = dadosSonda.lambdaRef;
        *o2Percent = dadosSonda.o2Percent;
        *controlValue = dadosSonda.controlValue;
       return ESP_OK;
    } else {
        // Fila vazia - isso é normal, não é erro
        return ESP_ERR_TIMEOUT;
    }
}
