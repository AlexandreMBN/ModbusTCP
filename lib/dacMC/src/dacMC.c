#include "dacMC.h"

dac_oneshot_handle_t dac_init(void){
    dac_oneshot_handle_t dac_handle;
    dac_oneshot_config_t dac_cfg = {
        .chan_id = DAC_CHAN_0   // GPIO25
    };

    // Inicializar o canal
    dac_oneshot_new_channel(&dac_cfg, &dac_handle);
    return dac_handle;
}
void dac_value(dac_oneshot_handle_t dac_handle, uint16_t val){
    dac_oneshot_output_voltage(dac_handle, val);
}

void dac_putValue(dac_oneshot_handle_t dac_handle, uint16_t varValue){
    uint16_t dacValue0=0;
    uint16_t maxDac = maxDac0;
    uint16_t minDac = minDac0;
    /* Escalona a variável de saída para valores entre eeMaxDac0 e eeMinDac0 (Valores de clibração do DAC) */
    dacValue0 = (uint16_t)((((uint32_t)varValue - MIN_VAR0_VAL) * ((uint32_t)maxDac - (uint32_t)minDac) / (MAX_VAR0_VAL - MIN_VAR0_VAL)) + (uint32_t)minDac);
	/* Limitação dos valores de máx e min que pode sair do DAC */
    if (dacValue0 < minDac)
    {
        dacValue0 = minDac;
    }
    else if (dacValue0 > maxDac)
    {
        dacValue0 = maxDac;
    }
    // Atualiza o valor do DAC
    dac_value(dac_handle, dacValue0);
}

void refreshDAC(dac_oneshot_handle_t dac_handle, float forcaValorDAC, float o2Percent){
    if(forcaValorDAC == 0){
        dac_putValue(dac_handle, o2Percent);
	}else{
		dac_value(dac_handle, forcaValorDAC);
	}
}