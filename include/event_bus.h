#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

// API pública para outros módulos sinalizarem eventos do sistema
// (implementada em main.c)

// Sinaliza início de Factory Reset
esp_err_t eventbus_factory_reset_start(void);

// Sinaliza conclusão de Factory Reset
esp_err_t eventbus_factory_reset_complete(void);

#ifdef __cplusplus
}
#endif

#endif // EVENT_BUS_H
