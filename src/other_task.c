#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modbus_slave_task.h"
#include "modbus_config.h"
#include "esp_log.h"            // for log_write
#include "freertos/semphr.h" // For critical sections or mutexes
extern SemaphoreHandle_t param_lock; // Use a mutex for protection
static const char *TAG = "OTHER_TASK";

void other_task(void *pvParameters)
{
    while (1)
    {
        // Example interaction with Modbus slave
        float new_value = 5.0; // New value to set in holding register
        ESP_LOGI(TAG, "Setting holding register value...");
        
        // Set the holding register value (assuming holding_data0 is accessible)
        // portENTER_CRITICAL(&param_lock);
        // holding_reg_params.holding_data0 = new_value;
        // portEXIT_CRITICAL(&param_lock);
        
        // ESP_LOGI(TAG, "Holding register value set to: %.2f", holding_reg_params.holding_data0);
        
        // Delay for a while before the next operation
        vTaskDelay(pdMS_TO_TICKS(1000)); // Delay for 1 second
    }
}

void start_other_task()
{
    xTaskCreate(other_task, "OtherTask", 2048, NULL, 5, NULL);
}