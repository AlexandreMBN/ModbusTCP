# ModbusTcpSlave Library CMake Configuration
cmake_minimum_required(VERSION 3.16)

# Set library name
set(COMPONENT_NAME "ModbusTcpSlave")

# Source files for the library
set(LIBRARY_SOURCES
    "src/modbus_tcp_slave.c"
)

# Header files (public interface)
set(LIBRARY_HEADERS
    "include/modbus_tcp_slave.h"
)

# FreeModbus source files - Common
set(FREEMODBUS_COMMON_SOURCES
    "freemodbus/common/esp_modbus_master.c"
    "freemodbus/common/esp_modbus_master_serial.c"
    "freemodbus/common/esp_modbus_master_tcp.c"
    "freemodbus/common/esp_modbus_slave.c"
    "freemodbus/common/esp_modbus_slave_serial.c"
    "freemodbus/common/esp_modbus_slave_tcp.c"
    "freemodbus/common/mb_endianness_utils.c"
)

# FreeModbus source files - Modbus Core
set(FREEMODBUS_MODBUS_SOURCES
    "freemodbus/modbus/mb.c"
    "freemodbus/modbus/mb_m.c"
    "freemodbus/modbus/ascii/mbascii.c"
    "freemodbus/modbus/ascii/mbascii_m.c"
    "freemodbus/modbus/functions/mbfunccoils.c"
    "freemodbus/modbus/functions/mbfunccoils_m.c"
    "freemodbus/modbus/functions/mbfuncdiag.c"
    "freemodbus/modbus/functions/mbfuncdisc.c"
    "freemodbus/modbus/functions/mbfuncdisc_m.c"
    "freemodbus/modbus/functions/mbfuncholding.c"
    "freemodbus/modbus/functions/mbfuncholding_m.c"
    "freemodbus/modbus/functions/mbfuncinput.c"
    "freemodbus/modbus/functions/mbfuncinput_m.c"
    "freemodbus/modbus/functions/mbfuncother.c"
    "freemodbus/modbus/functions/mbutils.c"
    "freemodbus/modbus/rtu/mbcrc.c"
    "freemodbus/modbus/rtu/mbrtu.c"
    "freemodbus/modbus/rtu/mbrtu_m.c"
    "freemodbus/modbus/tcp/mbtcp.c"
    "freemodbus/modbus/tcp/mbtcp_m.c"
)

# FreeModbus source files - Port
set(FREEMODBUS_PORT_SOURCES
    "freemodbus/port/port.c"
    "freemodbus/port/portevent.c"
    "freemodbus/port/portevent_m.c"
    "freemodbus/port/portother.c"
    "freemodbus/port/portother_m.c"
    "freemodbus/port/portserial.c"
    "freemodbus/port/portserial_m.c"
    "freemodbus/port/porttimer.c"
    "freemodbus/port/porttimer_m.c"
)

# FreeModbus source files - TCP Slave specific
set(FREEMODBUS_TCP_SLAVE_SOURCES
    "freemodbus/tcp_slave/modbus_controller/mbc_tcp_slave.c"
    "freemodbus/tcp_slave/port/port_tcp_slave.c"
)

# All source files
set(ALL_SOURCES
    ${LIBRARY_SOURCES}
    ${FREEMODBUS_COMMON_SOURCES}
    ${FREEMODBUS_MODBUS_SOURCES}
    ${FREEMODBUS_PORT_SOURCES}
    ${FREEMODBUS_TCP_SLAVE_SOURCES}
)

# Include directories
set(INCLUDE_DIRS
    "include"
    "freemodbus/common/include"
    "freemodbus/modbus/include"
    "freemodbus/port"
    "freemodbus/tcp_slave/modbus_controller"
    "freemodbus/tcp_slave/port"
)

# Create the library component
idf_component_register(
    SRCS ${ALL_SOURCES}
    INCLUDE_DIRS ${INCLUDE_DIRS}
    REQUIRES esp_netif esp_wifi lwip esp_event esp_timer log
    PRIV_REQUIRES esp_system
)

# Compiler definitions
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    MB_TCP_ENABLED=1
    CONFIG_FMB_TCP_ENABLED=1
    CONFIG_FMB_CONTROLLER_SLAVE_ID_SUPPORT=1
    CONFIG_FMB_CONTROLLER_NOTIFY_TIMEOUT=20000
    CONFIG_FMB_TCP_PORT_MAX_CONN=5
    CONFIG_FMB_TCP_CONNECTION_TIMEOUT_SEC=20
    CONFIG_FMB_TIMER_PORT_ENABLED=1
    CONFIG_FMB_EVENT_QUEUE_TIMEOUT=20
)

# Compiler options for FreeModbus
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-format
    -Wno-format-security
)

# Create alias for easier usage
add_library(ModbusTcpSlave ALIAS ${COMPONENT_LIB})

# Export configuration
set(ModbusTcpSlave_FOUND TRUE PARENT_SCOPE)
set(ModbusTcpSlave_INCLUDE_DIRS ${INCLUDE_DIRS} PARENT_SCOPE)
set(ModbusTcpSlave_LIBRARIES ${COMPONENT_LIB} PARENT_SCOPE)