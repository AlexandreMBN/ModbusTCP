#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <esp_http_server.h>

esp_err_t start_web_server();
esp_err_t root_get_handler(httpd_req_t *req);
esp_err_t login_get_handler(httpd_req_t *req);
esp_err_t do_login_handler(httpd_req_t *req);
esp_err_t logout_handler(httpd_req_t *req);
esp_err_t info_get_handler(httpd_req_t *req);
esp_err_t config_get_handler(httpd_req_t *req);
esp_err_t form_handler(httpd_req_t *req);
esp_err_t exit_get_handler(httpd_req_t *req);

esp_err_t modbus_config_get_handler(httpd_req_t *req); //exibe a página de configuração do Modbus
esp_err_t modbus_config_post_handler(httpd_req_t *req); //processa o formulário de configuração do Modbus

// Função para atualizar status WiFi
void set_wifi_status(const char* status);

#endif // WEBSERVER_H 