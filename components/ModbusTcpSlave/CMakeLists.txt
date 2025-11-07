# Thin component wrapper to include ModbusTcpSlave sources placed in lib/
# We reference files under ${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave so the
# original library layout in lib/ is preserved while ESP-IDF sees it as
# a component during CMake/IDF builds.

idf_component_register(
    SRCS
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/src/modbus_tcp_slave.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/common/esp_modbus_master.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/common/esp_modbus_master_serial.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/common/esp_modbus_master_tcp.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/common/esp_modbus_slave.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/common/esp_modbus_slave_serial.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/common/esp_modbus_slave_tcp.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/common/mb_endianness_utils.c"

        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/modbus/mb.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/modbus/mb_m.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/modbus/ascii/mbascii.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/modbus/ascii/mbascii_m.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/modbus/functions/mbfunccoils.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/modbus/functions/mbfunccoils_m.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/modbus/functions/mbfuncdiag.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/modbus/functions/mbfuncdisc.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/modbus/functions/mbfuncdisc_m.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/modbus/functions/mbfuncholding.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/modbus/functions/mbfuncholding_m.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/modbus/functions/mbfuncinput.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/modbus/functions/mbfuncinput_m.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/modbus/functions/mbfuncother.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/modbus/functions/mbutils.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/modbus/rtu/mbcrc.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/modbus/rtu/mbrtu.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/modbus/rtu/mbrtu_m.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/modbus/tcp/mbtcp.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/modbus/tcp/mbtcp_m.c"

        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/port/port.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/port/portevent.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/port/portevent_m.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/port/portother.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/port/portother_m.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/port/portserial.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/port/portserial_m.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/port/porttimer.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/port/porttimer_m.c"

        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/tcp_slave/modbus_controller/mbc_tcp_slave.c"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/tcp_slave/port/port_tcp_slave.c"
    INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/include"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/common/include"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/modbus/include"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/port"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/tcp_slave/modbus_controller"
        "${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/freemodbus/tcp_slave/port"
    REQUIRES esp_netif esp_wifi lwip esp_event esp_timer log
    PRIV_REQUIRES esp_system
)

# Keep the original component name available
set(ModbusTcpSlave_FOUND TRUE PARENT_SCOPE)
set(ModbusTcpSlave_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/lib/ModbusTcpSlave/include PARENT_SCOPE)
***