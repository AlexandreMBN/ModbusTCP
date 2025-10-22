#include "controle.h"


static float integral;
int16_t  erro=0, erroant=0;
uint32_t output=0;
int16_t cleanigDutty = 0;//10sec
int16_t cleanigDuttyHigh = 10;//10sec

void controle(spi_device_handle_t spi_cj125, adc_oneshot_unit_handle_t adc_handle, uint16_t heatRef){
    // static uint8_t IsCleanig = FALSE;
    // Retorna a leitura heat por meio do conversor AC
	heatValue = (int16_t) j125_get_heat(spi_cj125, adc_handle);
	erro= heatValue - heatRef;

    // Analisa temperatura está dentro do limite aceitável para uma correta medição do lambda
	if ((erro < 125) && (erro > -125))// Changed to 125. It was 100.
	{
    }

}

void controle_init(void){
    // inicializa o barramento SPI
    spi_device_handle_t spi_cj125_handle = cj125_init();
    
    // Inicializa o conversor AD
    adc_oneshot_unit_handle_t adc1_handle = adc_init();

    // inicializa a sonda 
    sonda_init();
    
    //Cj125 em modo calibração
	if (cj125_calib_mode(spi_cj125_handle)){
        printf("Calibrado com sucesso.");
    }
    // ver a necessidade de colocar um timer
    //  000/ portTICK_PERIOD_MS);
    
    // Coleta o valor do heat
    uint16_t heatRef = cj125_get_heat(spi_cj125_handle, adc1_handle);
    // // Coleta o valor do lambda
    uint16_t lambdaRef = cj125_get_lambda(spi_cj125_handle, adc1_handle);

    //CJ125 em modo sensor	
	cj125_sensor_mode(spi_cj125_handle);

    // Rampa de aquecimento da sonda
    sonda_pre_heating_ramp(spi_cj125_handle, adc1_handle);


}