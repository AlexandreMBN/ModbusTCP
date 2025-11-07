#ifndef FILAS_H
#define FILAS_H
    #include <stdint.h>
    #include "freertos/FreeRTOS.h"
    #include "freertos/queue.h"
    #include "esp_log.h"
    #include "stdio.h"
    #include "esp_err.h"

    #define TAM_FILA_SONDA 50
    #define TEMPO_ESPERA_FILA 0

    // Variável global da fila que receberá dados da sonda
    extern QueueHandle_t filaDadosSonda;

    // Estrutura de dados da sonda
    typedef struct {
        uint16_t heatValue;        // Valor do aquecedor
        int16_t lambdaValue;      // Valor do lambda
        uint16_t heatRef;          // Referência do aquecedor
        uint16_t lambdaRef;        // Referência do lambda
        uint16_t o2Percent;       // Percentual de oxigênio
        uint32_t controlValue;    // Saída do PID
    } sonda_data_t;
    
    // Preenche a estrutura de dados da sonda
    sonda_data_t preencheDadosSonda(uint16_t heatValue, int16_t lambdaValue, uint16_t heatRef, uint16_t lambdaRef, uint16_t o2Percent, uint32_t controlValue);

    esp_err_t inserirFilaSonda(uint16_t heatValue, int16_t lambdaValue, uint16_t heatRef, uint16_t lambdaRef, uint16_t o2Percent, uint32_t controlValue);
    
    esp_err_t removerFilaSonda(uint16_t *heatValue, int16_t *lambdaValue, uint16_t *heatRef, uint16_t *lambdaRef, uint16_t *o2Percent, uint32_t *controlValue);
    
    uint32_t elementosFilaPendentes(void);

#endif