#include "cj125.h"
#include "esp_log.h"
#include "globalvar.h"  // Para acesso às variáveis globais da sonda
// Erros
esp_err_t status;

static uint16_t mediaBufferO2[15];
static uint8_t mediaBufferCounter=0;

// configura e inicializa o barramento da SPI
void spi_buss_init(void){
    // Configura o barramento do SPI 
    spi_bus_config_t spi_buss_config = {
        .mosi_io_num = MOSI,
        .miso_io_num = MISO,
        .sclk_io_num = SCLK
    };
    // Inicializa o barramento
    status = spi_bus_initialize(SPI, &spi_buss_config,0);
	if (status == 0){
		printf("inicializa do barramento realizada com sucesso. Log: %d\n", status);
	}else{
    	printf("Erro ao inicializar o barramento. Log: %d\n", status);
	}
}

// configura e adiciona o dispositico cj125 no barramento da SPI
spi_device_handle_t cj125_init(void){
    // Cabeçalho do dispositivo cj125 no barramento
    spi_device_handle_t spi_cj125;
    
    // Inicializa o barramento
    spi_buss_init();

    // Configura o dispositivo cj125 no barramento
    spi_device_interface_config_t spi_device_config = {
        .clock_speed_hz = SPICLOCKSPEED, 
        .duty_cycle_pos = 128, 
        .mode = 1,
        .spics_io_num = CS,
        .queue_size = 1,
        .command_bits = 0,
        .address_bits = 0,
        .cs_ena_pretrans = 0
    };
    
    // Adiciona o dispositivo ao barramento 
    status = spi_bus_add_device(SPI, &spi_device_config, &spi_cj125);
	if (status == 0){
		printf("inicializa do dispositivo no barramento realizada com sucesso. Log: %d\n", status);
	}else{
		printf("Erro ao inicializa do dispositivo no barramento. Log: %d\n", status);
	}
    
    return spi_cj125;
}

void spi_write_single(spi_device_handle_t spi_handle, uint8_t dado, uint8_t rc_dado, uint16_t *rc_resposta){
    // //  Configuração da transação
    // spi_transaction_t spi_transaction_write = {
    //     .length = 8 ,// Quantidade dos bits de transferência
    //     .rx_buffer = rc_dado, // Armazena os dados recebidos
    //     .tx_buffer= dado, // Envio dos dados
    // };
    // // Realiza a transação SPI (recebe a string)
    // status = spi_device_transmit(spi_handle, &spi_transaction_write);
    // // printf("Transmitido: %X\n",*((char *)spi_transaction_write.tx_buffer));
	// uint8_t aux = dado;

	uint16_t tx_word = 0;

    // Combine two bytes into one 16-bit word
    tx_word = ((uint16_t)(dado) << 8) | (uint16_t)(rc_dado);

    // Send the 16-bit word and get the 16-bit response
    spi_transfer_16(spi_handle, tx_word, rc_resposta);

}

/* envia 16 bits (uma palavra) e recebe 16 bits de resposta */
esp_err_t spi_transfer_16(spi_device_handle_t spi_handle, uint16_t tx_word, uint16_t *rx_word) {
	spi_transaction_t t;
	memset(&t, 0, sizeof(t));

  // usamos o buffer interno (tx_data/rx_data) adequado para <=4 bytes
	t.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
	t.length = 16; // bits para enviar
  // Preenche tx_data em ordem de bytes: tx_data[0] = MSB, tx_data[1] = LSB
	t.tx_data[0] = (tx_word >> 8) & 0xFF;
	t.tx_data[1] = tx_word & 0xFF;

	esp_err_t ret = spi_device_transmit(spi_handle, &t);
	if (ret != ESP_OK) return ret;

  // rx_data contém bytes nas posições 0 e 1 (na mesma ordem do que o escravo enviou)
	*rx_word = ((uint16_t)t.rx_data[0] << 8) | (uint16_t)t.rx_data[1];
	return ESP_OK;
}


void spi_read_single(spi_device_handle_t spi_handle, uint8_t *dado){
    //  Configuração da transação
    spi_transaction_t spi_transaction_read = {
        .length = 8,//  Quantidade dos bits de transferência
        .rxlength = 8,
        .rx_buffer = dado, // Armazena os dados recebidos
    };
    // Realiza a transação SPI (recebe a string)
    status = spi_device_transmit(spi_handle, &spi_transaction_read);
    

}

uint8_t cj125_rx_tx_byte(spi_device_handle_t spi_cj125, uint8_t addr, uint8_t data){
	
    uint16_t data_received=0;
	uint8_t result = 0;
	
	//spi_write_single(spi_cj125, &addr, &data, &data_received);
	spi_write_single(spi_cj125, addr, data, &data_received);
	
	// printf("Add: %x\n",addr);
	// printf("Dt: %x\n",data);
	// printf("RC: %x\n",data_received);

	result = data_received>>8;
	//printf("rc: %x\n",result);
	if (((result) | 199) == 239){
		//spi_write_single(spi_cj125, &data,, &result);
		//printf("rc 2: %x\n",data);
		// printf("rc 2: %x\n",result);
	}
	else {
		result = 255;		
	}
	
    return result;

}

void cj125_err_clear(spi_device_handle_t spi_cj125){
	if (cj125_rx_tx_byte(spi_cj125, CJ125DIAGREG,SPIDUMMY) != 255){
		// printf("Diferente de 255");
	}else{
		// printf("igual a 255");
	}
	
}

bool cj125_calib_mode(spi_device_handle_t spi_cj125){

	// limpa o barramento
	cj125_err_clear(spi_cj125);
	
	int retorno = cj125_rx_tx_byte(spi_cj125, CJ125MODE,CALIBMODE);
	
	printf("log do atxmega: %d\n", retorno);
	if (retorno != 255){   
		return true;					
	}
	return false;	
}

void cj125_sensor_mode(spi_device_handle_t spi_cj125){
	cj125_err_clear(spi_cj125);
	
	if (cj125_rx_tx_byte(spi_cj125, CJ125MODE, SENSORMODE) != 255){
		ESP_LOGI("TAG", "Modo sensor ativado.");
	}
}

uint16_t cj125_get_heat(spi_device_handle_t spi_cj125, adc_oneshot_unit_handle_t  adc_handle){
	// limpa o barramento
	cj125_err_clear(spi_cj125);
	// retorna o valor lido do conversor no canal 7
	// uint16_t adc_mean_result = adc_get(adc2_handle, ADC_CHANNEL_4);
	uint16_t adc_mean_result = adc_get(adc_handle, ADC_CHANNEL_3);

	return adc_mean_result;
}

uint16_t cj125_get_lambda(spi_device_handle_t spi_cj125, adc_oneshot_unit_handle_t adc_handle){	// limpa o barramento
	cj125_err_clear(spi_cj125);
	// retorna o valor lido do conversor no canal 4
	// uint16_t adc_mean_result = adc_get(adc2_handle, ADC_CHANNEL_7)
	uint16_t adc_mean_result = adc_get(adc_handle, ADC_CHANNEL_4);
		
	return adc_mean_result;
}

uint16_t cj125_o2_calc(int16_t lambda){
	
	/*
	Método 01 - Calculo do IP de acordo com o material disponibilizado pelo alemão
	
	1-Ler o valor de offset do lambda (quando o cj está em modo de calibração)
	2-Subtrair o offset do valor lido do valor do lambda
	3-Utilizar a equação abaixo para cálculo do %O2	
	Ip=(((float)lambda)*2506.0*2.0)/(4095.0*62.0*17.0);
	
	------------
	
	M�todo 02 - IP de acordo com o datasheet da sonda LSU4.9
	
	IP � dado de acordo com a equa��o a seguir	
	Vcj125 = (((float)lambda)*2.506*2.0)/(4095.0)	
	IP = (Vcj125 - 1.5)/(0.0619*17.0)	
	*/
	
	float Ip=0, o2=0, Vcj125, Vclambdaref;
	uint16_t O2=0;
	uint16_t mediaMovelO2=0;
	float a,b,c,d,e;
	
	//Primeira calibra��o com g�s (aceit�vel, o sensor parou de funcionnar no meio da calibra��o)
// 	float a = 0.0002485;
// 	float b = -0.01106;
// 	float c = 0.1488;
// 	float d = 0.4297;
// 	float e = 1.599;
	
	//Segunda Calibra��o com g�s
// 	float a = 0.0001052;
// 	float b = -0.004416;
// 	float c = 0.04782;
// 	float d = 0.9944;
// 	float e = 0.4579;
		
	/* Método 02 */
	Vcj125 = (((float)(lambda))*2.506*2.0)/(4095.0);
	// Usa a variável global renomeada
	extern volatile int16_t sonda_lambdaRef_sync;
	Vclambdaref = (((float)(sonda_lambdaRef_sync))*2.506*2.0)/(4095.0);
	
	
	//Ip = (Vcj125-1.5)/(0.0619*17);
	Ip = (Vcj125- Vclambdaref)/(0.0619*17);
	/* M�todo 02 */
	
			
	//oxig�nio n�o pode ficar negativo...
// 	if (Ip <= -0.0401)
// 	{
// 		o2=0;
// 	}
// 	else
	
	//Calculo de O2
	//o2=(Ip+0.0401)/0.1214;
	o2=(Ip+0.0692)/0.1235;	// Planilha de calibra��o excel

	if (o2<0)
	{
		a = 0;
		b = 0;
		c = 0;
		d = 0;
		e = 0;
	}
	else if(o2 < 0.77)
	{
		//�rea 1 (0,1 a 1,8 do sensor) - Planilha de calibra��o excel
		a = 0;
		b = -8.429;	
		c = 11.71;
		d = -1.885;
		e = 0.1639;
	} 
	else if(o2 < 21)
	{
		//�rea 2+3 (1,9 a 21 do sensor) - Planilha de calibra��o excel
		a = 0.0001767;
		b = -0.007928;
		c = 0.1081;
		d = 0.5747;
		e = 1.425;			

	}
	else
	{
		a = 0;
		b = 0;
		c = 0;
		d = 0;
		e = 21.0;
	}
		
	o2 = a*o2*o2*o2*o2 + b*o2*o2*o2 + c*o2*o2 + d*o2 + e;
		
	if (o2 < 0)
	{
		o2 = 0;
	}
		
	else if (o2 > 21)
	{
		o2 = 21;
	}	
	
	O2=(uint16_t)(o2*100);	//*10
	
	//Media movel das 10 ultimas medi��es do % de O2
	mediaBufferO2[mediaBufferCounter++] = O2;
	
	if (mediaBufferCounter > 14)
	{
		mediaBufferCounter = 0;
	}
	
	for (uint8_t media=0; media <= 14; media++)
	{
		mediaMovelO2 += mediaBufferO2[media];
	}
	
	O2 = (mediaMovelO2/15);
	
	return O2;	
}

