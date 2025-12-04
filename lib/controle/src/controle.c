#include "controle.h"

void initControle(spi_device_handle_t spi_cj125, adc_oneshot_unit_handle_t adc_handle,  uint16_t *heatRef, uint16_t *lambdaRef){
    // Coloca o CJ125 em modo calibração
	if (cj125_calib_mode(spi_cj125)){
        ESP_LOGI(TAG, "Calibrado com sucesso.");
    }else{
        ESP_LOGI(TAG, "Erro ao calibrar.");
    }
    // Ler as referências de heat e lambda
    *heatRef = cj125_get_heat(spi_cj125, adc_handle);
    *lambdaRef = cj125_get_lambda(spi_cj125, adc_handle);

    // Espera um tempo
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    
    // Coloca o CJ125 em modo sensor
    cj125_sensor_mode(spi_cj125);
    
    // Inicializa o PWM da sonda
    sonda_init();
    
    // Aplica uma rampa de pré-aquecimento da sonda
    sonda_pre_heating_ramp(spi_cj125, adc_handle);
}

void controleTask(void *pvParameters) {
    // inicializa a comunição SPI do CJ125
    spi_device_handle_t spi_cj125_handle = cj125_init();
    
    // Inicializa o conversor AD do cj125
    adc_oneshot_unit_handle_t adc1_handle = adc_init();
    
    // Variáveis de controle
    int16_t erro = 0;
    uint32_t sinalControle=0;
    
    // Controlador
    float kp = 450.0f;
    float ki = 35.0f;
    float kd = 0.00f;
    PID pid_Temp;

    // Variáveis dos sensores
    int16_t heatValue = 0;
    int16_t lambdaValue = 0;
    uint16_t o2Percent=0;

    // Variáveis de referência dos sensores
    uint16_t heatRef = 0;
    uint16_t lambdaRef = 0;

    // Inicializa o controlador PID configurando os ganhos
    pid_set(&pid_Temp, kp, ki, kd, MAX_OUTPUT_VALUE);

    // Inicializa o controle da sonda (calibração, pré-aquecimento, PWM)
    initControle(spi_cj125_handle, adc1_handle, &heatRef, &lambdaRef);
    
    // Dados da sonda
    sonda_data_t dadosSonda;

    int cont=0;

    // Loop principal de controle da sonda
    while(true){
        // Lê o valor atual de heat
        heatValue = cj125_get_heat(spi_cj125_handle, adc1_handle);
        // Calcula o erro entre o valor medido e a referência desejada
        erro =  heatValue - heatRef;
        // Saída do controlador PID
        sinalControle = (uint32_t) pid_update(&pid_Temp, erro, DT);
       
		// Aplica o sinal de controle via PWM
		controle_2_pwm(sinalControle);
        
        // Analisa se a sonda está na faixa de operação
        if ((erro < 125) && (erro > -125)){
            // Armazena o valor de saída do controlador
            lambdaValue = cj125_get_lambda(spi_cj125_handle, adc1_handle);
            o2Percent = cj125_o2_calc(lambdaValue);

        }else{

        }
        cont++;
        if (cont>=100){
            cont=0;
            // Envia dados para a fila da sonda
            esp_err_t validacaoEscrita = inserirFilaSonda(heatValue, lambdaValue, heatRef, lambdaRef, o2Percent, sinalControle);
            if (validacaoEscrita != ESP_OK) {
                // Se a fila falhar, os dados ainda estarão nas variáveis globais
                ESP_LOGW(TAG, "Não foi possível enviar dados da sonda via fila");
            } else {
                ESP_LOGI(TAG, "Dados da sonda enviados via fila com sucesso");
            }
        // Tempo de espera para a próxima iteração
        vTaskDelay(10 / portTICK_PERIOD_MS); // 10 ms delay
        
    }
    
    
}