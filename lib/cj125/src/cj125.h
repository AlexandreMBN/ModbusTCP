#ifndef CJ123_H
#define CJ123_H
    // Biblioteca do esp32 do SPI host
    #include "driver/spi_master.h"
    #include "adcRio.h"
    #include <stdio.h>
    #include "globalvar.h"

    /* Configuraçõe do SPI  */
    // Pinos do barramento de comunicação SPI HSPI
    #define  MOSI 23
    #define  MISO 19
    #define  SCLK 18
    #define  CS 5
    #define SPI VSPI_HOST

    // estrutura do dispositivo conectado ao SPI
    // spi_device_handle_t spi_cj125;

    // Parâmetos do dispositivo cj125
    #define SPICLOCKSPEED 10*1000 //10 kHz


    #define SPIDUMMY		0
    /* Endereços CJ125*/
    /*Leitura*/
    #define CJ125ID			0x48 //retorna o ID do CJ [01100XXX] XXX ->versão do cj125
    #define CJ125DIAGREG	0x78 //indicador de falhas. deve ser lido antes de cada operação de R/W
    #define CJ125INITREG1	0x6C //indica o modo de operação atual 
    #define CJ125INITREG2	0x7E // 00010111

    /*Escrita*/
    #define CJ125MODE		0x56 //altera o modo de operação
    #define CONFIGREG		0x5A //altera o registrador INITREG2

    /* Modos de calibração */
    #define CALIBMODE		0x9D //modo de calibração
    #define SENSORMODE		0x89 //modo de operação normal (sensor de O2)

    // Inicializa o barramento
    void spi_buss_init(void);
    
    // Configura o barramento e os dispositivos conectados
    spi_device_handle_t cj125_init(void);

    // Escreve no barramento SPI
    //void spi_write_single(spi_device_handle_t spi_handle, uint8_t *dado, uint8_t *rc_dado);
    void spi_write_single(spi_device_handle_t spi_handle, uint8_t dado, uint8_t rc_dado, uint16_t *rc_resposta);
    esp_err_t spi_transfer_16(spi_device_handle_t spi_handle, uint16_t tx_word, uint16_t *rx_word);
    
    // Ler o barremento SPI
    void spi_read_single(spi_device_handle_t spi_handle, uint8_t *dado);
    
    // Manda e recebe mensagens do CJ125
    uint8_t cj125_rx_tx_byte(spi_device_handle_t spi_cj125, uint8_t addr, uint8_t data);
    
    
    // Limpa o barramento
    void cj125_err_clear(spi_device_handle_t spi_cj125);

    bool cj125_calib_mode(spi_device_handle_t spi_cj125);
    
    // Configura o CJ125 no modo sensor
    void cj125_sensor_mode(spi_device_handle_t spi_cj125);
    
    // Retorna a leitura heat por meio do conversor AC
    uint16_t cj125_get_heat(spi_device_handle_t spi_cj125, adc_oneshot_unit_handle_t adc_handle);

    // Retorna a leitura lambda por meio do conversor AC
    uint16_t cj125_get_lambda(spi_device_handle_t spi_cj125, adc_oneshot_unit_handle_t adc_handle);

    uint16_t cj125_o2_calc(int16_t lambda);

#endif