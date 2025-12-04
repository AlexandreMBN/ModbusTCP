#ifndef WSM_MIDDLEWARE_H
#define WSM_MIDDLEWARE_H

#include "web_server_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// MIDDLEWARE INTEGRADOS
// =============================================================================

/**
 * @brief Middleware de autenticação
 * @param req Requisição HTTP
 * @param user_data Nível de autenticação necessário (wsm_user_level_t*)
 * @return ESP_OK se autorizado, ESP_FAIL caso contrário
 */
esp_err_t wsm_auth_middleware(httpd_req_t *req, void *user_data);

/**
 * @brief Middleware de CORS
 * @param req Requisição HTTP
 * @param user_data Não utilizado
 * @return ESP_OK sempre
 */
esp_err_t wsm_cors_middleware(httpd_req_t *req, void *user_data);

/**
 * @brief Middleware de logging
 * @param req Requisição HTTP
 * @param user_data Não utilizado
 * @return ESP_OK sempre
 */
esp_err_t wsm_logging_middleware(httpd_req_t *req, void *user_data);

/**
 * @brief Middleware de rate limiting básico
 * @param req Requisição HTTP
 * @param user_data Configuração de rate limit
 * @return ESP_OK se dentro do limite, ESP_FAIL caso contrário
 */
esp_err_t wsm_rate_limit_middleware(httpd_req_t *req, void *user_data);

// =============================================================================
// FUNÇÕES AUXILIARES DE MIDDLEWARE
// =============================================================================

/**
 * @brief Executar pipeline de middleware
 * @param req Requisição HTTP
 * @return ESP_OK se todos passaram, ESP_FAIL caso contrário
 */
esp_err_t wsm_execute_middleware_pipeline(httpd_req_t *req);

/**
 * @brief Verificar se middleware está habilitado
 * @param type Tipo do middleware
 * @return true se habilitado
 */
bool wsm_is_middleware_enabled(wsm_middleware_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* WSM_MIDDLEWARE_H */