#ifndef CONTROLE_H
#define CONTROLE_H
    #include <string.h>
    #include <stdio.h>
    #include "cj125.h"
    #include "globalvar.h"
    #include "sonda.h"
    #include "adcRio.h"
    #include "PID_Interface.h"
    // Definições do timer de amostragem do controlador
    #define SAMPLETIMER		TCC1
    #define SAMPLETIMERPS	( 1024UL )
    #define SAMPLETIMESEG	0.1		

    // Definições do controlador
    #define KP 18.0 //Era 20 em 21/01/2021
    #define KI 2.5
    #define DT SAMPLETIMESEG

    // Variáveis do controlador
    #define MAX_OUTPUT_VALUE 170000 //Valores de saturação do controlador
    #define MIN_OUTPUT_VALUE 0

    void controle_init(void);
#endif