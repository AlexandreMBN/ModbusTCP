# 📋 Implementação dos Handlers MQTT/TLS no WebServer

## ✅ Página Criada:
- ✅ `data/html/mqtt_config.html` - Interface completa para configuração MQTT/TLS
- ✅ Link adicionado em `admin.html` (seção administrativa)

## 🔧 Próximos Passos - Implementação no WebServer (src/webserver.c):

### 1. Adicionar Headers/Includes
```c
#include "mqtt_client_task.h"  // Para acessar funções MQTT
```

### 2. Declarações de Handlers (adicionar após linha ~77)
```c
// MQTT Configuration Handlers
esp_err_t mqtt_config_get_handler(httpd_req_t *req);
esp_err_t mqtt_config_post_handler(httpd_req_t *req);
esp_err_t mqtt_test_post_handler(httpd_req_t *req);
esp_err_t mqtt_status_get_handler(httpd_req_t *req);
esp_err_t mqtt_cert_upload_post_handler(httpd_req_t *req);
```

### 3. Registrar URIs (adicionar na função start_web_server)
```c
// MQTT Configuration Routes
httpd_uri_t mqtt_config_get_uri = {
    .uri = "/mqtt_config",
    .method = HTTP_GET,
    .handler = mqtt_config_get_handler
};

httpd_uri_t mqtt_config_post_uri = {
    .uri = "/api/mqtt/config",
    .method = HTTP_POST,
    .handler = mqtt_config_post_handler
};

httpd_uri_t mqtt_test_post_uri = {
    .uri = "/api/mqtt/test",
    .method = HTTP_POST,
    .handler = mqtt_test_post_handler
};

httpd_uri_t mqtt_status_get_uri = {
    .uri = "/api/mqtt/status",
    .method = HTTP_GET,
    .handler = mqtt_status_get_handler
};

httpd_uri_t mqtt_cert_upload_post_uri = {
    .uri = "/api/mqtt/upload_cert",
    .method = HTTP_POST,
    .handler = mqtt_cert_upload_post_handler
};

// Registrar os handlers
httpd_register_uri_handler(server, &mqtt_config_get_uri);
httpd_register_uri_handler(server, &mqtt_config_post_uri);
httpd_register_uri_handler(server, &mqtt_test_post_uri);
httpd_register_uri_handler(server, &mqtt_status_get_uri);
httpd_register_uri_handler(server, &mqtt_cert_upload_post_uri);
```

### 4. Implementar Handlers

#### 4.1 Handler para Página de Configuração (GET /mqtt_config)
```c
esp_err_t mqtt_config_get_handler(httpd_req_t *req) {
    // Verificar autenticação de admin
    if (!check_admin_auth(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Acesso negado", -1);
        return ESP_FAIL;
    }
    
    char *html_template = NULL;
    size_t html_len = 0;
    
    // Carregar template HTML
    esp_err_t ret = load_file_to_string("/spiffs/mqtt_config.html", &html_template, &html_len);
    if (ret != ESP_OK) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    
    // Obter configuração atual
    mqtt_config_t current_config;
    mqtt_get_current_config(&current_config);  // Função a implementar
    
    // Buffer para HTML processado
    char *processed_html = malloc(html_len + 2048);
    if (!processed_html) {
        free(html_template);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    // Substituir placeholders
    strcpy(processed_html, html_template);
    
    // Substituições básicas
    replace_placeholder(processed_html, "{{MQTT_BROKER_URL}}", current_config.broker_url);
    replace_placeholder(processed_html, "{{MQTT_PORT}}", itoa(current_config.port));
    replace_placeholder(processed_html, "{{MQTT_CLIENT_ID}}", current_config.client_id);
    replace_placeholder(processed_html, "{{MQTT_USERNAME}}", current_config.username);
    replace_placeholder(processed_html, "{{MQTT_PASSWORD}}", current_config.password);
    
    // Substituições booleanas
    replace_placeholder(processed_html, "{{MQTT_TLS_CHECKED}}", 
                       current_config.tls_enabled ? "checked" : "");
    replace_placeholder(processed_html, "{{MQTT_RETAIN_CHECKED}}", 
                       current_config.retain ? "checked" : "");
    
    // Substituições de seleção
    replace_placeholder(processed_html, "{{MQTT_TLS_ENABLED}}", 
                       current_config.tls_enabled ? "true" : "false");
    
    // QoS selecionado
    replace_placeholder(processed_html, "{{MQTT_QOS_0_SELECTED}}", 
                       current_config.qos == 0 ? "selected" : "");
    replace_placeholder(processed_html, "{{MQTT_QOS_1_SELECTED}}", 
                       current_config.qos == 1 ? "selected" : "");
    replace_placeholder(processed_html, "{{MQTT_QOS_2_SELECTED}}", 
                       current_config.qos == 2 ? "selected" : "");
    
    // Certificado selecionado
    replace_placeholder(processed_html, "{{MQTT_CA_DEFAULT_SELECTED}}", 
                       strcmp(current_config.ca_path, "/spiffs/isrgrootx1.pem") == 0 ? "selected" : "");
    replace_placeholder(processed_html, "{{MQTT_CA_CUSTOM_SELECTED}}", 
                       strcmp(current_config.ca_path, "/spiffs/custom_ca.pem") == 0 ? "selected" : "");
    
    // Outros valores
    char interval_str[16];
    snprintf(interval_str, sizeof(interval_str), "%lu", current_config.publish_interval_ms);
    replace_placeholder(processed_html, "{{MQTT_PUBLISH_INTERVAL}}", interval_str);
    
    // Enviar resposta
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, processed_html, strlen(processed_html));
    
    // Limpeza
    free(html_template);
    free(processed_html);
    
    return ESP_OK;
}
```

#### 4.2 Handler para Salvar Configuração (POST /api/mqtt/config)
```c
esp_err_t mqtt_config_post_handler(httpd_req_t *req) {
    // Verificar autenticação
    if (!check_admin_auth(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        const char* resp = "{\"success\":false,\"message\":\"Não autorizado\"}";
        httpd_resp_send(req, resp, strlen(resp));
        return ESP_FAIL;
    }
    
    char buf[1024];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        const char* resp = "{\"success\":false,\"message\":\"Dados inválidos\"}";
        httpd_resp_send(req, resp, strlen(resp));
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    // Parse JSON
    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        const char* resp = "{\"success\":false,\"message\":\"JSON inválido\"}";
        httpd_resp_send(req, resp, strlen(resp));
        return ESP_FAIL;
    }
    
    // Extrair configuração
    mqtt_config_t new_config = {0};
    
    cJSON *broker = cJSON_GetObjectItem(json, "broker_url");
    if (broker && cJSON_IsString(broker)) {
        strncpy(new_config.broker_url, broker->valuestring, sizeof(new_config.broker_url) - 1);
    }
    
    cJSON *port = cJSON_GetObjectItem(json, "port");
    if (port && cJSON_IsNumber(port)) {
        new_config.port = port->valueint;
    }
    
    cJSON *client_id = cJSON_GetObjectItem(json, "client_id");
    if (client_id && cJSON_IsString(client_id)) {
        strncpy(new_config.client_id, client_id->valuestring, sizeof(new_config.client_id) - 1);
    }
    
    cJSON *username = cJSON_GetObjectItem(json, "username");
    if (username && cJSON_IsString(username)) {
        strncpy(new_config.username, username->valuestring, sizeof(new_config.username) - 1);
    }
    
    cJSON *password = cJSON_GetObjectItem(json, "password");
    if (password && cJSON_IsString(password)) {
        strncpy(new_config.password, password->valuestring, sizeof(new_config.password) - 1);
    }
    
    cJSON *tls_enabled = cJSON_GetObjectItem(json, "tls_enabled");
    if (tls_enabled && cJSON_IsBool(tls_enabled)) {
        new_config.tls_enabled = cJSON_IsTrue(tls_enabled);
    }
    
    cJSON *ca_path = cJSON_GetObjectItem(json, "ca_path");
    if (ca_path && cJSON_IsString(ca_path)) {
        strncpy(new_config.ca_path, ca_path->valuestring, sizeof(new_config.ca_path) - 1);
    }
    
    cJSON *qos = cJSON_GetObjectItem(json, "qos");
    if (qos && cJSON_IsNumber(qos)) {
        new_config.qos = qos->valueint;
    }
    
    cJSON *retain = cJSON_GetObjectItem(json, "retain");
    if (retain && cJSON_IsBool(retain)) {
        new_config.retain = cJSON_IsTrue(retain);
    }
    
    cJSON *publish_interval = cJSON_GetObjectItem(json, "publish_interval");
    if (publish_interval && cJSON_IsNumber(publish_interval)) {
        new_config.publish_interval_ms = publish_interval->valueint;
    }
    
    new_config.enabled = true;
    
    // Aplicar configuração
    esp_err_t config_ret = mqtt_set_config(&new_config);
    
    // Resposta
    const char* resp;
    if (config_ret == ESP_OK) {
        resp = "{\"success\":true,\"message\":\"Configuração salva com sucesso\"}";
    } else {
        resp = "{\"success\":false,\"message\":\"Erro ao aplicar configuração\"}";
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    
    cJSON_Delete(json);
    return ESP_OK;
}
```

#### 4.3 Handler para Testar Conexão (POST /api/mqtt/test)
```c
esp_err_t mqtt_test_post_handler(httpd_req_t *req) {
    // Verificar autenticação
    if (!check_admin_auth(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        const char* resp = "{\"success\":false,\"message\":\"Não autorizado\"}";
        httpd_resp_send(req, resp, strlen(resp));
        return ESP_FAIL;
    }
    
    // Implementar teste de conexão temporário
    // (similar ao mqtt_config_post_handler mas sem salvar)
    
    const char* resp = "{\"success\":true,\"message\":\"Conexão testada com sucesso\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    
    return ESP_OK;
}
```

#### 4.4 Handler para Status MQTT (GET /api/mqtt/status)
```c
esp_err_t mqtt_status_get_handler(httpd_req_t *req) {
    mqtt_state_t state = mqtt_get_state();
    
    const char* status_str;
    const char* message;
    
    switch (state) {
        case MQTT_STATE_CONNECTED:
            status_str = "connected";
            message = "Conectado";
            break;
        case MQTT_STATE_CONNECTING:
            status_str = "connecting";
            message = "Conectando...";
            break;
        case MQTT_STATE_ERROR:
            status_str = "disconnected";
            message = "Erro de Conexão";
            break;
        default:
            status_str = "disconnected";
            message = "Desconectado";
            break;
    }
    
    char resp[128];
    snprintf(resp, sizeof(resp), 
             "{\"success\":true,\"status\":\"%s\",\"message\":\"%s\"}",
             status_str, message);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    
    return ESP_OK;
}
```

#### 4.5 Handler para Upload de Certificado (POST /api/mqtt/upload_cert)
```c
esp_err_t mqtt_cert_upload_post_handler(httpd_req_t *req) {
    // Verificar autenticação
    if (!check_admin_auth(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        const char* resp = "{\"success\":false,\"message\":\"Não autorizado\"}";
        httpd_resp_send(req, resp, strlen(resp));
        return ESP_FAIL;
    }
    
    // Implementar upload de arquivo multipart/form-data
    // Salvar como /spiffs/custom_ca.pem
    // Validar formato PEM
    
    const char* resp = "{\"success\":true,\"message\":\"Certificado enviado com sucesso\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    
    return ESP_OK;
}
```

### 5. Funções Auxiliares a Implementar

#### 5.1 Verificação de Autenticação
```c
bool check_admin_auth(httpd_req_t *req) {
    // Implementar verificação de sessão de admin
    // Retornar true se usuário está autenticado como admin
    return true; // Placeholder
}
```

#### 5.2 Obter Configuração Atual
```c
esp_err_t mqtt_get_current_config(mqtt_config_t *config) {
    // Implementar para obter configuração atual do MQTT
    // Pode ser da variável global ou NVS
    return ESP_OK;
}
```

#### 5.3 Substituição de Placeholders
```c
void replace_placeholder(char *html, const char *placeholder, const char *value) {
    char *pos = strstr(html, placeholder);
    if (pos != NULL) {
        size_t placeholder_len = strlen(placeholder);
        size_t value_len = strlen(value);
        memmove(pos + value_len, pos + placeholder_len, strlen(pos + placeholder_len) + 1);
        memcpy(pos, value, value_len);
    }
}
```

## 🎯 Resultado Final

Após implementar todos os handlers:

1. ✅ **Usuário admin** verá link "🔐 Configurar MQTT/TLS" na página administrativa
2. ✅ **Interface completa** com todas as opções TLS/SSL
3. ✅ **Upload de certificados** personalizada (drag & drop)
4. ✅ **Teste de conexão** em tempo real
5. ✅ **Validação** e feedback visual
6. ✅ **Segurança** - apenas admins acessam

## 📝 Ordem de Implementação Recomendada

1. **Primeiro:** Handler básico `mqtt_config_get_handler` (mostrar página)
2. **Segundo:** Handler `mqtt_status_get_handler` (status atual)
3. **Terceiro:** Handler `mqtt_config_post_handler` (salvar configuração)
4. **Quarto:** Handler `mqtt_test_post_handler` (testar conexão)
5. **Último:** Handler `mqtt_cert_upload_post_handler` (upload certificado)

Isso permite implementação gradual e teste a cada etapa!