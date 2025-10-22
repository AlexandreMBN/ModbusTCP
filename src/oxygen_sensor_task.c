#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include "cj125.h"
#include "globalvar.h"
#include "sonda.h"
#include "adcRio.h"
#include "esp_log.h"
#include "PID.h"
#include "oxygen_sensor_task.h"
#include "mqtt_client_task.h"

#define LED_GPIO_PIN    GPIO_NUM_2  // GPIO2, commonly used for onboard LED on ESP32

#define KP				1.0f //Era 20 em 21/01/2021
#define KI				2.5f
#define DT				0.0f // 10ms
// static float integral = 0.0f;

#define MAX_OUTPUT_VALUE	170000 //Valores de satura��o do controlador.
#define MIN_OUTPUT_VALUE	0

static const char *TAG = "SONDA_CONTROL";


void sonda_control_task(void *pvParameters) {
 spi_device_handle_t spi_cj125_handle = cj125_init();

    
    // Inicializa o conversor AD
    adc_oneshot_unit_handle_t adc1_handle = adc_init();
    


    int16_t erro = 0;
    int16_t heatValue = 0;
    int16_t lambdaValue = 0;
    uint16_t o2Percent=0;
    uint32_t output=0;
    PID pid_Temp;
    pid_set(&pid_Temp ,450.0f ,35.0f ,0.00f ,MAX_OUTPUT_VALUE);
    // Coleta o valor do heat
    // uint16_t heatRef = cj125_get_lambda(spi_cj125_handle, adc2_handle);
    // uint16_t lambdaRef = cj125_get_heat(spi_cj125_handle, adc1_handle);

    //Cj125 em modo calibração
	if (cj125_calib_mode(spi_cj125_handle)){
        ESP_LOGI(TAG, "Calibrado com sucesso.");
    }else{
        ESP_LOGI(TAG, "Erro ao calibrar.");
    }
    // while(true){
        uint16_t heatRef = cj125_get_heat(spi_cj125_handle, adc1_handle);
        uint16_t lambdaRef = cj125_get_lambda(spi_cj125_handle, adc1_handle);
        
        lambdaRef =  adjust_adc_result(lambdaRef);

        ESP_LOGI(TAG, "Valor do heat: %d", heatRef);
        ESP_LOGI(TAG, "Valor do lambda: %d", lambdaRef);
        vTaskDelay(2000 / portTICK_PERIOD_MS); // 20 second delay
    // }
    
    
    // Coloca o CJ125 em modo sensor
    cj125_sensor_mode(spi_cj125_handle);
    // Inicializa a sonda e o pwm
    sonda_init();

    sonda_pre_heating_ramp(spi_cj125_handle, adc1_handle);
    int cont=0;   
    while(true){
        heatValue = cj125_get_heat(spi_cj125_handle, adc1_handle);
        
        erro =  heatValue - heatRef;
 
        long ctrl_output = pid_update(&pid_Temp, erro, 0.01);
        if (ctrl_output<=0)
		{
			ctrl_output=0;
		}
		output = (uint32_t)ctrl_output;
		controle_2_pwm(output);
        if ((erro < 125) && (erro > -125)){
            lambdaValue = cj125_get_lambda(spi_cj125_handle, adc1_handle);
            uint16_t lambdaValueEscala = adjust_adc_result(lambdaValue);
            o2Percent = cj125_o2_calc(lambdaValueEscala);//Cálculo do %O2

        }else{

        }
        
        // ========== SINCRONIZAÇÃO COM VARIÁVEIS GLOBAIS PARA MODBUS ==========
        // Atualiza as variáveis globais que serão lidas pela task Modbus
        extern volatile int16_t sonda_heatValue_sync;
        extern volatile int16_t sonda_lambdaValue_sync;
        extern volatile int16_t sonda_heatRef_sync;
        extern volatile int16_t sonda_lambdaRef_sync;
        extern volatile uint16_t sonda_o2Percent_sync;
        extern volatile uint32_t sonda_output_sync;
        
        // Sincroniza dados (thread-safe)
        sonda_heatValue_sync = heatValue;
        sonda_lambdaValue_sync = lambdaValue;
        sonda_heatRef_sync = heatRef;
        sonda_lambdaRef_sync = lambdaRef;
        sonda_o2Percent_sync = o2Percent;
        sonda_output_sync = output;
        // // integral = integral + erro*DT;
	    // output=KP*erro + KI*integral;

        // if(output>=MAX_OUTPUT_VALUE){
        //     output=MAX_OUTPUT_VALUE;	
        // }	
        
        // if(output<=MIN_OUTPUT_VALUE){
        //     output=MIN_OUTPUT_VALUE;
        // }
        
        // controle_2_pwm(40000);
        cont++;
        if (cont>=100){
            cont=0;
            ESP_LOGI(TAG, "Valor do heat: %d", heatValue);
            ESP_LOGI(TAG, "Valor do erro: %d", erro);
            ESP_LOGI(TAG, "Valor do lambda: %d", lambdaValue);
            ESP_LOGI(TAG, "Valor do O2: %d", o2Percent);
            ESP_LOGI(TAG, "Valor do u: %ld", ctrl_output);
            ESP_LOGI(TAG, "___________________________________________________________\n");
            
            // Envia dados para MQTT (não bloqueia se falhar)
            esp_err_t mqtt_ret = mqtt_send_data_to_queue(heatValue, lambdaValue, erro, o2Percent, (uint32_t)ctrl_output);
            if (mqtt_ret != ESP_OK && mqtt_ret != ESP_ERR_INVALID_STATE) {
                ESP_LOGD(TAG, "Dados MQTT não enviados: %s", esp_err_to_name(mqtt_ret));
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS); // 10 ms delay
        
    }
    
    
}