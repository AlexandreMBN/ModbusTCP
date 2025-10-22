#ifndef __MODBUS_TASK_H__
#define __MODBUS_TASK_H__

#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Modbus task function.
 *
 * This function handles Modbus communication events by checking for read/write
 * operations and logging the details. It should be run as a FreeRTOS task.
 *
 * @param pvParameters Not used.
 */
void modbus_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // __MODBUS_TASK_H__