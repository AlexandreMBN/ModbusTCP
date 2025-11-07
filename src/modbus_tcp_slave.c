/*
 * NOTE: This file previously contained a full Modbus TCP slave implementation.
 * The project also contains the library implementation under
 * lib/ModbusTcpSlave which provides the same public API. Having two
 * implementations compiled creates duplicate internal FreeModbus globals
 * (mbcontroller state) and leads to the "Slave interface is not correctly
 * initialized" error at runtime. To avoid that, this source has been reduced
 * to a no-op stub so the library implementation is used exclusively.
 */

#include "modbus_tcp_slave.h"

/* Intentionally empty - implementation provided by lib/ModbusTcpSlave */
