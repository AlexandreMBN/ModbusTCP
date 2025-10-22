#ifndef ADCRIO_H
#define ADCRIO_H
    #include "esp_adc/adc_oneshot.h"
    #define ANALOG_INPUTS 4 //Quantidade de entradas analógicas.
    #define ADC_GAIN 1 //Tens�o de Ref 2.5V , tens�o divisor de tens�o 2.35
   
    // inicializa o conversor AD
    adc_oneshot_unit_handle_t adc_init();

    uint16_t adjust_adc_result(uint16_t adc_result);
    // Retorna o valor lido do conversor pelo canal escolhido
    uint16_t adc_get(adc_oneshot_unit_handle_t adc_handle, adc_channel_t channel);
#endif