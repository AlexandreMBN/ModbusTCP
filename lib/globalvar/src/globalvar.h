/*
 * globalvar.h
 *
 * Created: 06/06/2014 15:02:43
 *  Author: DPM
 */ 


#ifndef GLOBALVAR_H_
#define GLOBALVAR_H_
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

// #define ERROR_CODE_LEN 5
// enum  eErrors
// {
// 	PROBE_TEMP_OUT_OF_RANGE,
// 	COMPRESSOR_FAIL,
// 	ERROR1,
// 	ERROR2,
// 	ERROR3,
// };

// enum  gProcessState
// {
// 	IDLE,
// 	STARTING,
// 	UPDATE_CONFIG,
// 	ADIM,
// 	CLEANIG,
// };

// /* Var�veis de debug */
// extern volatile uint16_t tempVect[300];
// extern volatile uint8_t	tempFlag;

// /****** Flag do LCD ******/
// extern volatile uint8_t LCD_flag;

/* Vari�veis de Medi��o*/
extern volatile uint16_t o2Percent;
/* Vari�veis do processo  */
// extern volatile uint8_t sMonitorState;
// /* Calibra��o da sa�da anal�gica */
// extern volatile uint16_t	max_dac_corrente;
// extern volatile uint16_t	min_dac_corrente;
// // extern volatile uint16_t	max_dac_tensao;
// // extern volatile uint16_t	min_dac_tensao;
// extern volatile uint8_t dACOffset0;
// extern volatile uint8_t dACGain0;

// extern volatile uint16_t forcaValorDAC;

/* Valores de leitura do ADC (heat e lambda) - RENOMEADAS PARA EVITAR CONFLITO */
extern volatile int16_t sonda_heatRef;
extern volatile int16_t sonda_heatValue;
extern volatile int16_t sonda_lambdaRef;
extern volatile int16_t sonda_lambdaValue;

/* Variáveis globais para sincronização Modbus (atualizadas pela MCT Task) */
extern volatile int16_t sonda_heatRef_sync;
extern volatile int16_t sonda_heatValue_sync;
extern volatile int16_t sonda_lambdaRef_sync;
extern volatile int16_t sonda_lambdaValue_sync;
extern volatile uint16_t sonda_o2Percent_sync;
extern volatile uint32_t sonda_output_sync;

// /* Carrega de calibracao das entradas anal�gicas */
// extern volatile uint16_t offsetEA0C;
// extern volatile uint16_t gainEA0C;
// extern volatile uint16_t offsetEA0T;
// extern volatile uint16_t gainEA0T;
// 
// extern volatile uint16_t offsetEA1C;
// extern volatile uint16_t gainEA1C;
// extern volatile uint16_t offsetEA1T;
// extern volatile uint16_t gainEA1T;
// 
// extern volatile uint16_t offsetEA2C;
// extern volatile uint16_t gainEA2C;
// extern volatile uint16_t offsetEA2T;
// extern volatile uint16_t gainEA2T;
// 
// extern volatile uint16_t offsetEA3C;
// extern volatile uint16_t gainEA3C;
// extern volatile uint16_t offsetEA3T;
// extern volatile uint16_t gainEA3T;

// /* Carrega valores m�ximo e m�nimo de calibra��o dos dacs */
// extern volatile uint16_t maxDac0;
// extern volatile uint16_t minDac0;
// extern volatile uint16_t maxDac1;
// extern volatile uint16_t minDac1;

// /* Valores do registrador DAC */
// extern volatile uint16_t	saidaAnalogicaDac0;
// //extern volatile uint16_t	saidaAnalogicaDac1;


// /* Temperatura M�xima */
// // extern volatile uint16_t saveTempMax;  // Temperatura m�xima registrada na caixa
// // extern volatile uint16_t tempNow;

// /* Vari�vel para captura do esfor�o de controle */
// extern volatile uint32_t output_mb;

// /* contador para habilitar rel� de alarme */
// extern volatile uint16_t countEnableAlarmRelay;

// extern volatile uint16_t errorCode[ERROR_CODE_LEN];

// /* inicializa todas as vari�veis globais da aplica��o */
// void carrega_variaveis_globais(void);

// //linear interpolation function
// long fMap(long in, long in_min, long in_max, long out_min, long out_max);

#endif /* GLOBALVAR_H_ */