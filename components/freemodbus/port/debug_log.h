#ifndef _DEBUG_LOG_H
#define _DEBUG_LOG_H

#include "esp_log.h"

#define MB_TCP_DEBUG_TAG "MB_TCP"

#define MB_TCP_DEBUG_PRINT(fmt, ...) \
    ESP_LOGI(MB_TCP_DEBUG_TAG, fmt, ##__VA_ARGS__)

#define MB_TCP_ERROR_PRINT(fmt, ...) \
    ESP_LOGE(MB_TCP_DEBUG_TAG, fmt, ##__VA_ARGS__)

#endif // _DEBUG_LOG_H