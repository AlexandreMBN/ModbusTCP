#ifndef CONTROLE_H
#define CONTROLE_H
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include <string.h>
    #include <stdio.h>
    #include "cj125.h"
    #include "globalvar.h"
    #include "sonda.h"
    #include "adcRio.h"
    #include "PID.h"
    #include "esp_log.h"
    #include "mqtt_client_task.h"
    #include "Filas.h"
    // GPIO2, commonly used for onboard LED on ESP32
    #define LED_GPIO_PIN    GPIO_NUM_2  
    
    //Valores de saturação do controlador.
    #define MAX_OUTPUT_VALUE 170000 
    #define MIN_OUTPUT_VALUE 0
    
    // Tempo de amostragem do controle
    #define DT 0.01f  // 10 ms

    static const char *TAG = "SONDA_CONTROL";


    void initControle(spi_device_handle_t spi_cj125, adc_oneshot_unit_handle_t adc_handle,  uint16_t *heatRef, uint16_t *lambdaRef);
    void controleTask(void *pvParameters);
#endif