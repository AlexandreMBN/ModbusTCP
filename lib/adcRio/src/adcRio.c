#include "adcRio.h"

uint16_t adca_offset;


adc_oneshot_unit_handle_t adc_init(){
    // Variável do conversor ADC 1 e ADC 2
    adc_oneshot_unit_handle_t adc1_handle;

    // Configuração do conversor ADC1
    adc_oneshot_unit_init_cfg_t init_config_adc1 = {
        .unit_id = ADC_UNIT_1,
    };
    

    // Inicializa o conversor
    adc_oneshot_new_unit(&init_config_adc1, &adc1_handle);
    
    
    // Configuração comum para os canais
    adc_oneshot_chan_cfg_t chan_config = {
        .atten = ADC_ATTEN_DB_12,  // atenuação valores de referência (3V3)
        // .atten = ADC_ATTEN_DB_6,  // atenuação valores de referência (3V3)
        .bitwidth = ADC_BITWIDTH_12 // precisão/resolução de 12 bis
    };

    // Configura os canais 4, 5, 6 e 7
    adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_4, &chan_config); // pino 32
    adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_3, &chan_config); // pino 39
    // adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_6, &chan_config);
    // adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_7, &chan_config);
    return adc1_handle;
}

uint16_t adjust_adc_result(uint16_t adc_result){
	float Vref_in = 3.3; // Tensão de referência do ADC
    float Vref_out = 2.5; // Tensão de referência desejada
    uint32_t BITS = 4095; // Resolução do ADC (12 bits)
    float adcEscala = (float) adc_result*(Vref_out/Vref_in);// + 0.5f;
    // retorna zeros se o valor for negativo
    if ( adcEscala<0.0f) adcEscala=0.0f;
    uint32_t adc_out = (uint32_t) adcEscala;

    if (adc_out > BITS) adc_out = BITS;

    return (uint16_t) adc_out;
		// if(adc_result<=adca_offset) adc_result=0;
		// else{
		// 	adc_result-=(adca_offset);
		// 	adc_result= adc_result*ADC_GAIN;
		// 	if(adc_result>4095) adc_result=4095;
		// }
		
		// return adc_result;
}



uint16_t adc_get(adc_oneshot_unit_handle_t adc_handle, adc_channel_t channel){
    // Armazena o valor lido pelo conversor
    int valor_adc4;
    // Ler o valor do conversor
    adc_oneshot_read(adc_handle, channel, &valor_adc4);
    return valor_adc4;
}


//int valor_adc4;adc_oneshot_read(adc1_handle, ADC_CHANNEL_4, &valor_adc4);
// {
// //	cli();

// 	uint16_t result=0; 

// 	//adc_correct();
	
// // 	switch (adc)
// // 	{
// // 		//ADCA
// // 		case 0:
// 		if (adcChan == 0)
// 		{
// 			result = adc_mean_result[0];
// 		}
// 		else if (adcChan == 1)
// 		{
// 			result = adc_mean_result[1];
// 		}
// 		else if (adcChan == 2)
// 		{
// 			result = adc_mean_result[2];
// 		}
// 		else if (adcChan == 3)
// 		{
// 			result = adc_mean_result[3];
// 		}

// //		break;
// //	}
// //	sei();
// 	return result;
// }