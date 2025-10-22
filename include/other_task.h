#ifndef __OTHER_TASK_H__
#define __OTHER_TASK_H__

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief FreeRTOS task function.
 *
 * This task demonstrates an example of interacting with Modbus registers,
 * specifically by periodically setting a value in a holding register.
 *
 * @param pvParameters Not used.
 */
void other_task(void *pvParameters);

/**
 * @brief Creates and starts the other_task.
 *
 * This function initializes and runs the other_task with a specified stack size and priority.
 */
void start_other_task();

#ifdef __cplusplus
}
#endif

#endif // __OTHER_TASK_H__      