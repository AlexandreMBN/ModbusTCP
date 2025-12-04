

# Define todas as pastas de include do FreeModbus
set(priv_include_dirs
    freemodbus/common
    freemodbus/common/include
    freemodbus/port
    freemodbus/modbus
    freemodbus/modbus/functions
    freemodbus/modbus/rtu
    freemodbus/modbus/tcp
    # freemodbus/modbus/ascii
    freemodbus/modbus/include
    freemodbus/serial_slave/port
    freemodbus/serial_slave/modbus_controller
    # freemodbus/serial_master/port
    # freemodbus/serial_master/modbus_controller
    freemodbus/tcp_slave/port
    freemodbus/tcp_slave/modbus_controller
    # freemodbus/tcp_master/port
    # freemodbus/tcp_master/modbus_controller
)

# Procura automaticamente todos os .c dentro de freemodbus
file(GLOB_RECURSE priv_srcs
    # "freemodbus/common/*.c"
    # "freemodbus/port/*.c"
    # "freemodbus/modbus/*.c"
    # "freemodbus/modbus/functions/*.c"
    # "freemodbus/modbus/rtu/*.c"
    # # "freemodbus/modbus/tcp/*.c"
    # "freemodbus/serial_slave/port/*.c"
    # "freemodbus/serial_slave/modbus_controller/*.c"
    # # "freemodbus/serial_master/port/*.c"
    # # "freemodbus/serial_master/modbus_controller/*.c"
    # # "freemodbus/tcp_slave/port/*.c"
    # # "freemodbus/tcp_slave/modbus_controller/*.c"
    # # "freemodbus/tcp_master/port/*.c"
    # # "freemodbus/tcp_master/modbus_controller/*.c"

    # Núcleo do Modbus
    "freemodbus/modbus/mb.c"
    "freemodbus/modbus/mb_m.c"
    "freemodbus/modbus/functions/mbfunccoils.c"
    "freemodbus/modbus/functions/mbfuncdisc.c"
    "freemodbus/modbus/functions/mbfuncholding.c"
    "freemodbus/modbus/functions/mbfuncinput.c"
    "freemodbus/modbus/functions/mbutils.c"
    "freemodbus/modbus/functions/mbfuncother.c"
    "freemodbus/modbus/rtu/mbrtu.c"
    "freemodbus/modbus/rtu/mbcrc.c"
    "freemodbus/modbus/tcp/mbtcp.c"
    "freemodbus/common/esp_modbus_slave_serial.c"
    "freemodbus/common/esp_modbus_slave_tcp.c"
    
    # Portabilidade
    "freemodbus/port/port.c"
    "freemodbus/port/portevent.c"
    "freemodbus/port/portevent_m.c"
    "freemodbus/port/portother.c"
    "freemodbus/port/portother_m.c"
    "freemodbus/port/portserial.c"
    "freemodbus/port/portserial_m.c"
    "freemodbus/port/porttimer.c"
    "freemodbus/port/porttimer_m.c"

    # Parte "common" necessária
    "freemodbus/common/esp_modbus_slave.c"

    # Controlador RTU Slave
    "freemodbus/serial_slave/modbus_controller/mbc_serial_slave.c"

    # Controlador TCP Slave
    "freemodbus/tcp_slave/port/port_tcp_slave.c"
    "freemodbus/tcp_slave/modbus_controller/mbc_tcp_slave.c"



)

idf_component_register(
    SRCS ${priv_srcs}
    INCLUDE_DIRS ${priv_include_dirs}
    REQUIRES driver freertos esp_timer esp_netif lwip
)

# Habilita Modbus TCP com todas as configurações necessárias
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    CONFIG_FMB_COMM_MODE_TCP_EN=1
    MB_TCP_ENABLED=1
    CONFIG_FMB_TCP_PORT_DEFAULT=502
    CONFIG_FMB_TCP_PORT_MAX_CONN=5
    CONFIG_FMB_TCP_CONNECTION_TOUT_SEC=20
    CONFIG_FMB_TCP_UID_ENABLED=0
)

    

    