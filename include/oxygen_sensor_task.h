#ifndef __OXYGEN_SENSOR_TASK_H__
#define __OXYGEN_SENSOR_TASK_H__

#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sonda Control task function.
 *
 * This function handles the oxygen sensor (lambda probe) control including
 * PID temperature control, sensor readings, and data synchronization.
 * It should be run as a FreeRTOS task.
 *
 * @param pvParameters Not used.
 */
void sonda_control_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // __OXYGEN_SENSOR_TASK_H__