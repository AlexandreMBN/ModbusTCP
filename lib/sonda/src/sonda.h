#ifndef SONDA_H_
#define SONDA_H_
    // Biblioteca de geração do PWM
    #include "driver/ledc.h"
    #include "cj125.h"
    /*Definições do PWM de aquecimento da sonda*/
    #define PWMFREQHZ 100
    #define PWMCHANNEL LEDC_CHANNEL_0
    #define SPEED_MODE LEDC_HIGH_SPEED_MODE // PWM gerado por hardware
    #define TIMER LEDC_TIMER_0
    #define PWMPIN 21
    #define PWMMAX 200000

    void sonda_init(void);

    void controle_2_pwm(uint32_t controle);

    void sonda_pre_heating_ramp(spi_device_handle_t spi_cj125, adc_oneshot_unit_handle_t adc_handle);

#endif