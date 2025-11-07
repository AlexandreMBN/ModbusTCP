#ifndef MODBUS_MAP_H
#define MODBUS_MAP_H

#define REG_CONFIG_START		1000      
#define REG_CONFIG_SIZE			3        

#define REG_DATA_START			2000    
#define REG_DATA_SIZE			1      

#define REG_3000_START			3000        
#define REG_3000_SIZE			2            

#define REG_4000_START			4000       
#define REG_4000_SIZE			8           

#define REG_5000_START			5000
#define REG_5000_SIZE			8 
    
#define REG_6000_START			6000    
#define REG_6000_SIZE			5         

#define REG_7000_START			7000
#define REG_7000_SIZE			8

#define REG_8000_START			8000
#define REG_8000_SIZE			8

#define REG_UNITSPECS_START		9000
#define REG_UNITSPECS_SIZE		20

enum reg1000_config {
    baudrate,
    endereco,
    paridade
};

enum reg2000_config {
    dataValue
};

enum reg3000_config {
    maxDac,
    minDac
};

enum reg4000_config {
    lambdaValue,
    lambdaRef,
    heatValue,
    heatRef,
    output_mb,
    PROBE_DEMAGED,
    PROBE_TEMP_OUT_OF_RANGE,
    COMPRESSOR_FAIL
};

enum reg5000_config {
    teste1,
    teste2,
    teste3,
    teste4
};

enum reg6000_config {
    maxDac0,
    forcaValorDAC,
    nada,
    dACGain0,
    dACOffset0
};

enum reg9000_config {
    valorZero,
    valorUm,
    firmVerHi,
    firmVerLo,
    valorQuatro,
    valorCinco,
    lotnum0,
    lotnum1,    
    lotnum2,
    lotnum3,
    lotnum4,
    lotnum5,
    wafnum,
    coordx0,
    coordx1,
    coordy0,
    coordy1,
    valor17,
    valor18,
    valor19
};

#endif // MODBUS_MAP_H