#include "sonda.h"
#include "esp_log.h"

void sonda_init(void){
    // Configuração do timer do PWM
    ledc_timer_config_t timer_config = {
        .speed_mode = SPEED_MODE, // modulo do velocidade
        .duty_resolution  = LEDC_TIMER_16_BIT, // Resolução (Bits)
        .timer_num = TIMER, // Escolher qual o timer 
        .freq_hz = PWMFREQHZ, // Taxa de atualização (Hz) do PWM            
        .clk_cfg = LEDC_AUTO_CLK // Configurações da fonte de clock 
    };
    // Inicializa e configura o timer
    ledc_timer_config(&timer_config);

    // Configuração do canal do PWM
    ledc_channel_config_t channel_config = {
        .gpio_num = PWMPIN, //Pino
        .speed_mode = SPEED_MODE, // Modulo de velocidade
        .channel = PWMCHANNEL, // Canal do gerado do pwm
        .timer_sel = TIMER, // Timer
        .duty  = 0, // Inicializar o dutycicle igual a zero
        .hpoint = 0
    };

    // Inicializa e configura o timer
    ledc_channel_config(&channel_config);
}

void controle_2_pwm(uint32_t controle){
    uint32_t dutyCycle =  ( ((float)controle / PWMMAX) * 65535 ); // Converte o valor do controle (0-200000) para o dutycycle (0-65535)
    // Acionamento do PWM
    ledc_set_duty(SPEED_MODE, PWMCHANNEL, dutyCycle);
    // Atualiza para o hadware/sofware 
    ledc_update_duty(SPEED_MODE, PWMCHANNEL);
}

void sonda_pre_heating_ramp(spi_device_handle_t spi_cj125, adc_oneshot_unit_handle_t adc2_handle){   
    // Se a temperatura estiver menor do que o ponto de operação
	uint16_t heat = cj125_get_heat(spi_cj125, adc2_handle);//cj125_get_heat(spi_cj125, adc_handle);
    if (heat > 850 ){
        // ESP_LOGI("TAG", "Aquecido");
	}else{
		// Tensão inicial da sonda em 4V
		uint32_t vInit = 0.33*PWMMAX;	
	
        // Aplica uma rampa em 5 segundos
		for(int8_t k=0; k<=5; k++){
            heat = cj125_get_heat(spi_cj125, adc2_handle);
            // Aplica o pwm
            controle_2_pwm(vInit);
            // Espera um segundo
			vTaskDelay(1000/portTICK_PERIOD_MS);
			// Rampa de 0.4V/s
			vInit += (0.033*PWMMAX);
            ESP_LOGI("TAG", "Valor do heat: %u", heat);
		}
	}	
}	
