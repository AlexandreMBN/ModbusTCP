/*
 * globalvar.c
 *
 * Created: 06/05/2015 17:09:24
 *  Author: DPM
 */ 

#include "globalvar.h"
#include <stdlib.h>
#include <stdio.h>


volatile uint16_t o2Percent=0;

// /* Vari�veis do processo  */
// volatile uint8_t sMonitorState;

// /****** Flag do LCD ******/
// volatile uint8_t LCD_flag = false;


/* Referência do CJ125 - RENOMEADAS PARA EVITAR CONFLITO */
volatile int16_t sonda_heatRef=0;
volatile int16_t sonda_heatValue=0;
volatile int16_t sonda_lambdaRef=0;
volatile int16_t sonda_lambdaValue=0;

/* Variáveis globais para sincronização Modbus (atualizadas pela MCT Task) */
volatile int16_t sonda_heatRef_sync=0;
volatile int16_t sonda_heatValue_sync=0;
volatile int16_t sonda_lambdaRef_sync=0;
volatile int16_t sonda_lambdaValue_sync=0;
volatile uint16_t sonda_o2Percent_sync=0;
volatile uint32_t sonda_output_sync=0;

// /* Carrega valores m�ximo e m�nimo dos dacs */
// volatile uint16_t maxDac0 = 0;
// volatile uint16_t minDac0 = 0;

// volatile uint8_t dACOffset0 = 0;
// volatile uint8_t dACGain0 = 1;

// volatile uint16_t forcaValorDAC=0;
// /* Valores do registrador DAC */
// volatile uint16_t saidaAnalogicaDac0 = 0;

// /* Vari�vel para captura do esfor�o de controle */
// volatile uint32_t output_mb = 0;

// /* contador para habilitar rel� de alarme */
// volatile uint16_t countEnableAlarmRelay = 0;

// /* Esplanador de codigos de Erro*/
// volatile uint16_t errorCode[ERROR_CODE_LEN];//={1,2,3,4,5};

// void carrega_variaveis_globais(void)
// {
// 	/* Carrega valores m�ximo e m�nimo dos dacs */
// 	maxDac0 = eeprom_read_word(&eeMaxDac0);
// 	minDac0 = eeprom_read_word(&eeMinDac0);
// 	dACGain0 = eeprom_read_byte(&eeDACGain0);
// 	dACOffset0 = eeprom_read_byte(&eeDACOffset0);	
// }

// long fMap(long in, long in_min, long in_max, long out_min, long out_max)
// {
// 	return (in - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
// }