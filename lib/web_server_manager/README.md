# Web Server Manager Library

Uma biblioteca completa e profissional para gerenciamento de servidor web no ESP32, desenvolvida para integração com o ecossistema de bibliotecas ESP32.

## 📋 Índice

- [Características](#características)
- [Arquitetura](#arquitetura)
- [Instalação](#instalação)
- [Uso Rápido](#uso-rápido)
- [Configuração](#configuração)
- [Sistema de Autenticação](#sistema-de-autenticação)
- [Sistema de Templates](#sistema-de-templates)
- [Middleware](#middleware)
- [Arquivos Estáticos](#arquivos-estáticos)
- [Handlers Padrão](#handlers-padrão)
- [API Completa](#api-completa)
- [Exemplos](#exemplos)
- [Integração](#integração)

## 🚀 Características

### Core Features
- ✅ **Servidor HTTP Completo**: Baseado no ESP-IDF HTTP server
- ✅ **Sistema de Roteamento**: Registro e gerenciamento de rotas
- ✅ **Autenticação e Sessões**: Sistema baseado em cookies com níveis de usuário
- ✅ **Engine de Templates**: Sistema de substituição com cache
- ✅ **Pipeline de Middleware**: CORS, logging, rate limiting, security headers
- ✅ **Arquivos Estáticos**: Servir CSS, JS, imagens com cache e ETag
- ✅ **Handlers Integrados**: Login, logout, informações do sistema, restart

### Recursos Avançados
- 🔐 **Múltiplos Níveis de Usuário**: None, Basic, Admin
- 📊 **Estatísticas de Uso**: Monitoramento de requisições e performance
- 🛡️ **Segurança**: Headers de segurança, rate limiting, validação
- 💾 **Cache Inteligente**: Templates e arquivos estáticos
- 🔄 **Hot Reload**: Recarga dinâmica de configurações
- 📱 **Responsivo**: Suporte a dispositivos móveis

### Integração
- 🌐 **WiFi Manager**: Integração completa com gerenciamento WiFi
- 📡 **MQTT Client Manager**: Status e controle MQTT via web
- ⚙️ **Config Manager**: Interface web para configurações
- 💿 **SPIFFS File Manager**: Gerenciamento de arquivos via web

## 🏗️ Arquitetura

```
Web Server Manager
├── Core (web_server_manager.c)
│   ├── Contexto global
│   ├── Configuração
│   ├── Roteamento
│   └── Estatísticas
├── Authentication (wsm_auth.c)
│   ├── Sessões baseadas em cookies
│   ├── Múltiplos usuários
│   └── Níveis de acesso
├── Templates (wsm_templates.c)
│   ├── Engine de substituição
│   ├── Cache de templates
│   └── Validação
├── Middleware (wsm_middleware.c)
│   ├── CORS
│   ├── Logging
│   ├── Rate Limiting
│   └── Security Headers
├── Static Files (wsm_static_files.c)
│   ├── Servir arquivos
│   ├── MIME types
│   ├── Cache e ETag
│   └── Compressão
└── Handlers (wsm_handlers.c)
    ├── Login/Logout
    ├── System Info
    ├── Restart
    └── Factory Reset
```

## 📦 Instalação

### 1. Copiar arquivos
```bash
# Copiar biblioteca para o projeto
cp -r lib/web_server_manager /path/to/your/project/lib/
```

### 2. Configurar CMakeLists.txt
```cmake
# No CMakeLists.txt principal
set(COMPONENT_REQUIRES 
    esp_http_server 
    nvs_flash 
    spiffs
    web_server_manager
)
```

### 3. Incluir headers
```c
#include "web_server_manager.h"
```

## ⚡ Uso Rápido

### Setup Básico
```c
#include "web_server_manager.h"

void app_main(void)
{
    // Configuração básica
    wsm_config_t config = {0};  // Usar padrões
    
    // Configurar usuário admin
    strcpy(config.auth_config.users[0].username, "admin");
    strcpy(config.auth_config.users[0].password, "admin123");
    config.auth_config.users[0].level = WSM_USER_LEVEL_ADMIN;
    
    // Inicializar e iniciar
    ESP_ERROR_CHECK(wsm_init(&config));
    ESP_ERROR_CHECK(wsm_register_default_handlers());
    ESP_ERROR_CHECK(wsm_start());
    
    ESP_LOGI("APP", "Web Server started on port 80");
}
```

### Handler Customizado
```c
static esp_err_t my_handler(httpd_req_t *req)
{
    const char *html = "<h1>Minha Página</h1>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, strlen(html));
}

// Registrar rota
wsm_route_config_t route = {
    .uri = "/minha-pagina",
    .method = HTTP_GET,
    .handler = my_handler,
    .required_level = WSM_USER_LEVEL_BASIC,
    .middleware = NULL,
    .require_auth = true
};

wsm_register_route(&route);
```

## ⚙️ Configuração

### Estrutura de Configuração
```c
typedef struct {
    httpd_config_t server_config;        // Configuração do servidor HTTP
    wsm_auth_config_t auth_config;       // Configuração de autenticação
    wsm_template_config_t template_config; // Configuração de templates
    wsm_static_config_t static_config;   // Configuração de arquivos estáticos
} wsm_config_t;
```

### Exemplo Completo
```c
wsm_config_t config = {
    .server_config = {
        .task_priority = 5,
        .stack_size = 8192,
        .max_uri_handlers = 20,
        .max_open_sockets = 7,
        .backlog_conn = 5,
        .lru_purge_enable = true,
        .recv_wait_timeout = 5,
        .send_wait_timeout = 5
    },
    .auth_config = {
        .session_timeout = 1800,  // 30 minutos
        .max_sessions = 5,
        .require_auth = true,
        .users = {
            {"admin", "admin123", WSM_USER_LEVEL_ADMIN},
            {"user", "user123", WSM_USER_LEVEL_BASIC},
            {NULL, NULL, WSM_USER_LEVEL_NONE}
        }
    },
    .template_config = {
        .cache_enabled = true,
        .max_cached_templates = 10,
        .template_root = "/spiffs/templates"
    },
    .static_config = {
        .cache_enabled = true,
        .max_cached_files = 20,
        .static_root = "/spiffs/static",
        .enable_etag = true,
        .enable_compression = true
    }
};
```

## 🔐 Sistema de Autenticação

### Níveis de Usuário
```c
typedef enum {
    WSM_USER_LEVEL_NONE = 0,   // Não autenticado
    WSM_USER_LEVEL_BASIC = 1,  // Usuário básico
    WSM_USER_LEVEL_ADMIN = 2   // Administrador
} wsm_user_level_t;
```

### Gerenciamento de Sessões
```c
// Verificar autenticação
esp_err_t ret = wsm_check_auth(req, WSM_USER_LEVEL_ADMIN);
if (ret != ESP_OK) {
    return ret; // Redirecionamento automático para login
}

// Obter nível do usuário
wsm_user_level_t level = wsm_get_user_level(req);

// Fazer logout
wsm_logout_session(req);
```

### Configuração de Usuários
```c
wsm_auth_config_t auth_config = {
    .session_timeout = 3600,  // 1 hora
    .max_sessions = 10,
    .require_auth = true,
    .users = {
        {"root", "toor", WSM_USER_LEVEL_ADMIN},
        {"operator", "op123", WSM_USER_LEVEL_BASIC},
        {"guest", "guest", WSM_USER_LEVEL_BASIC},
        // Terminar com entrada NULL
        {NULL, NULL, WSM_USER_LEVEL_NONE}
    }
};
```

## 📄 Sistema de Templates

### Estrutura de Template
```html
<!DOCTYPE html>
<html>
<head>
    <title>{{PAGE_TITLE}}</title>
</head>
<body>
    <h1>{{DEVICE_NAME}}</h1>
    <p>Status WiFi: {{WIFI_STATUS}}</p>
    <p>Uptime: {{UPTIME}}</p>
</body>
</html>
```

### Usar Templates
```c
// Preparar contexto
wsm_template_context_t context;
wsm_template_context_init(&context);

// Adicionar substituições
wsm_template_add_substitution(&context, "PAGE_TITLE", "Sistema ESP32");
wsm_template_add_substitution(&context, "DEVICE_NAME", "Meu ESP32");
wsm_template_add_substitution(&context, "WIFI_STATUS", "Conectado");
wsm_template_add_substitution(&context, "UPTIME", "2 horas");

// Renderizar template
esp_err_t ret = wsm_respond_with_template(req, "status.html", &context);

// Limpar contexto
wsm_template_context_cleanup(&context);
```

### Cache de Templates
```c
// Templates são automaticamente cached quando:
// - cache_enabled = true na configuração
// - Template é carregado com sucesso
// - Espaço disponível no cache

// Limpar cache manualmente
wsm_template_clear_cache();

// Recarregar template específico
wsm_template_reload("status.html");
```

## 🔧 Middleware

### Middleware Disponível
- **CORS**: Cross-Origin Resource Sharing
- **Logging**: Log de todas as requisições
- **Rate Limiting**: Limitação de taxa de requisições
- **Security Headers**: Headers de segurança automáticos

### Configuração de Middleware
```c
wsm_middleware_config_t middleware_config = {
    .cors_enabled = true,
    .cors_origins = "*",
    .cors_methods = "GET,POST,PUT,DELETE",
    .cors_headers = "Content-Type,Authorization",
    .logging_enabled = true,
    .rate_limit_enabled = true,
    .rate_limit_requests = 100,  // 100 requisições
    .rate_limit_window = 60,     // por minuto
    .security_headers_enabled = true
};

wsm_configure_middleware(&middleware_config);
```

### Middleware Customizado
```c
static esp_err_t my_middleware(httpd_req_t *req)
{
    ESP_LOGI("MIDDLEWARE", "Request: %s %s", 
             http_method_str(req->method), req->uri);
    
    // Adicionar header customizado
    httpd_resp_set_hdr(req, "X-Custom-Header", "MyValue");
    
    return ESP_OK;
}

// Usar em uma rota
wsm_route_config_t route = {
    .uri = "/api/data",
    .method = HTTP_GET,
    .handler = my_handler,
    .middleware = my_middleware,  // Aplicar middleware
    .required_level = WSM_USER_LEVEL_BASIC
};
```

## 📁 Arquivos Estáticos

### Estrutura de Diretórios
```
/spiffs/static/
├── css/
│   ├── style.css
│   └── admin.css
├── js/
│   ├── app.js
│   └── utils.js
├── images/
│   ├── logo.png
│   └── favicon.ico
└── fonts/
    └── roboto.ttf
```

### Configuração
```c
wsm_static_config_t static_config = {
    .cache_enabled = true,
    .max_cached_files = 50,
    .static_root = "/spiffs/static",
    .enable_etag = true,
    .enable_compression = true  // Para arquivos de texto
};
```

### MIME Types Suportados
- **Texto**: html, css, js, txt, xml, json
- **Imagens**: png, jpg, jpeg, gif, svg, ico
- **Fontes**: ttf, woff, woff2
- **Áudio/Vídeo**: mp3, mp4, avi, wav
- **Outros**: pdf, zip

### Headers Automáticos
```
Content-Type: application/javascript
Cache-Control: public, max-age=86400
ETag: "abc123def456"
Content-Encoding: gzip  (se aplicável)
```

## 🛠️ Handlers Padrão

### Login System
- `GET /login` - Página de login
- `POST /login` - Processar login
- `GET /logout` - Fazer logout

### System Management
- `GET /system/info` - Informações do sistema
- `GET /system/restart` - Página de restart
- `POST /system/restart` - Executar restart
- `GET /system/factory-reset` - Página de factory reset
- `POST /system/factory-reset` - Executar factory reset

### Uso
```c
// Registrar todos os handlers padrão
ESP_ERROR_CHECK(wsm_register_default_handlers());

// Ou registrar individualmente
wsm_route_config_t login_route = {
    .uri = "/login",
    .method = HTTP_GET,
    .handler = wsm_login_handler,
    .required_level = WSM_USER_LEVEL_NONE
};
wsm_register_route(&login_route);
```

## 📋 API Completa

### Inicialização e Controle
```c
esp_err_t wsm_init(const wsm_config_t *config);
esp_err_t wsm_start(void);
esp_err_t wsm_stop(void);
esp_err_t wsm_restart(void);
esp_err_t wsm_deinit(void);
bool wsm_is_running(void);
```

### Roteamento
```c
esp_err_t wsm_register_route(const wsm_route_config_t *route);
esp_err_t wsm_register_routes(const wsm_route_config_t *routes, size_t count);
esp_err_t wsm_unregister_route(const char *uri, httpd_method_t method);
esp_err_t wsm_unregister_all_routes(void);
```

### Autenticação
```c
esp_err_t wsm_check_auth(httpd_req_t *req, wsm_user_level_t required_level);
wsm_user_level_t wsm_get_user_level(httpd_req_t *req);
esp_err_t wsm_set_user_level(httpd_req_t *req, wsm_user_level_t level);
esp_err_t wsm_logout_session(httpd_req_t *req);
esp_err_t wsm_process_login(const char *username, const char *password, wsm_user_level_t *level);
```

### Templates
```c
esp_err_t wsm_respond_with_template(httpd_req_t *req, const char *template_name, 
                                   const wsm_template_context_t *context);
esp_err_t wsm_template_context_init(wsm_template_context_t *context);
esp_err_t wsm_template_add_substitution(wsm_template_context_t *context, 
                                       const char *key, const char *value);
void wsm_template_context_cleanup(wsm_template_context_t *context);
```

### Middleware
```c
esp_err_t wsm_configure_middleware(const wsm_middleware_config_t *config);
esp_err_t wsm_apply_cors(httpd_req_t *req);
esp_err_t wsm_apply_security_headers(httpd_req_t *req);
esp_err_t wsm_check_rate_limit(httpd_req_t *req);
```

### Utilitários
```c
esp_err_t wsm_get_stats(wsm_stats_t *stats);
esp_err_t wsm_reset_stats(void);
esp_err_t wsm_url_decode_inplace(char *str);
esp_err_t wsm_get_client_ip(httpd_req_t *req, char *ip_str, size_t ip_str_len);
```

## 💡 Exemplos

### Exemplo 1: API REST
```c
static esp_err_t api_devices_handler(httpd_req_t *req)
{
    const char *json = "{\"devices\":[{\"id\":1,\"name\":\"ESP32\",\"status\":\"online\"}]}";
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, strlen(json));
}

wsm_route_config_t api_route = {
    .uri = "/api/devices",
    .method = HTTP_GET,
    .handler = api_devices_handler,
    .required_level = WSM_USER_LEVEL_BASIC,
    .require_auth = true
};
```

### Exemplo 2: Upload de Arquivo
```c
static esp_err_t upload_handler(httpd_req_t *req)
{
    char filepath[128];
    FILE *fd = NULL;
    
    // Extrair nome do arquivo
    if (httpd_req_get_hdr_value_str(req, "Content-Disposition", 
                                   filepath, sizeof(filepath)) == ESP_OK) {
        // Processar upload
        fd = fopen("/spiffs/uploads/file.dat", "w");
        if (fd) {
            char buf[1024];
            int bytes_read;
            while ((bytes_read = httpd_req_recv(req, buf, sizeof(buf))) > 0) {
                fwrite(buf, 1, bytes_read, fd);
            }
            fclose(fd);
        }
    }
    
    httpd_resp_sendstr(req, "Upload concluído");
    return ESP_OK;
}
```

### Exemplo 3: WebSocket (Extensão)
```c
// Para WebSocket, pode-se estender o Web Server Manager
static esp_err_t websocket_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI("WS", "WebSocket connection requested");
        return ESP_OK;
    }
    return ESP_FAIL;
}
```

## 🔗 Integração

### Com WiFi Manager
```c
#include "wifi_manager.h"

// No handler
wifi_manager_status_t status = wifi_manager_get_status();
if (status == WIFI_MANAGER_CONNECTED) {
    wifi_manager_get_ip_info(&ip_info);
    // Usar informações de IP
}
```

### Com MQTT Client Manager
```c
#include "mqtt_client_manager.h"

// Status MQTT na página web
mqtt_client_status_t mqtt_status = mqtt_client_manager_get_status();
wsm_template_add_substitution(&context, "MQTT_STATUS", 
    (mqtt_status == MQTT_CLIENT_CONNECTED) ? "Conectado" : "Desconectado");
```

### Com Config Manager
```c
#include "config_manager.h"

// Carregar configurações via web
static esp_err_t config_load_handler(httpd_req_t *req)
{
    cJSON *config = config_manager_get_config("system");
    if (config) {
        char *json_string = cJSON_Print(config);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_string, strlen(json_string));
        free(json_string);
        cJSON_Delete(config);
    }
    return ESP_OK;
}
```

## 📈 Monitoramento e Debug

### Logs
```c
// Habilitar logs detalhados
esp_log_level_set("WSM", ESP_LOG_DEBUG);
esp_log_level_set("WSM_AUTH", ESP_LOG_DEBUG);
esp_log_level_set("WSM_TEMPLATE", ESP_LOG_DEBUG);
```

### Estatísticas
```c
wsm_stats_t stats;
wsm_get_stats(&stats);

ESP_LOGI("STATS", "Total requests: %lu", stats.requests_total);
ESP_LOGI("STATS", "Success: %lu, Errors: %lu", 
         stats.requests_success, stats.requests_error);
ESP_LOGI("STATS", "Active sessions: %u", stats.active_sessions);
```

### Debugging
```c
// Verificar status do servidor
if (wsm_is_running()) {
    ESP_LOGI("DEBUG", "Web server is running");
} else {
    ESP_LOGE("DEBUG", "Web server is not running");
}

// Verificar autenticação
wsm_user_level_t level = wsm_get_user_level(req);
ESP_LOGI("DEBUG", "User level: %d", level);
```

## 🚨 Tratamento de Erros

### Códigos de Erro Comuns
- `ESP_ERR_INVALID_ARG` - Parâmetros inválidos
- `ESP_ERR_INVALID_STATE` - Estado inválido (ex: servidor já iniciado)
- `ESP_ERR_NO_MEM` - Memória insuficiente
- `ESP_ERR_NOT_FOUND` - Recurso não encontrado
- `ESP_FAIL` - Falha geral

### Handling
```c
esp_err_t ret = wsm_init(&config);
if (ret != ESP_OK) {
    ESP_LOGE("APP", "Failed to initialize Web Server Manager: %s", 
             esp_err_to_name(ret));
    return;
}
```

## 📝 Notas de Desenvolvimento

### Limitações Atuais
- Máximo de 10 usuários simultâneos por padrão
- Cache limitado para templates e arquivos estáticos
- WebSocket não implementado (pode ser adicionado)
- Upload de arquivos grandes pode causar timeout

### Roadmap
- [ ] Suporte a WebSocket
- [ ] Upload de arquivos com progress
- [ ] Autenticação JWT
- [ ] Rate limiting por usuário
- [ ] Compressão automática de respostas
- [ ] Suporte a HTTPS/TLS

### Contribuindo
1. Fork do repositório
2. Criar branch para feature
3. Implementar com testes
4. Documentar mudanças
5. Abrir Pull Request

## 📄 Licença

Este projeto está licenciado sob a MIT License - veja o arquivo LICENSE para detalhes.

## 👥 Créditos

Desenvolvido como parte do ecossistema de bibliotecas ESP32 para aplicações IoT profissionais.

---

**Web Server Manager Library v1.0.0**  
*Uma solução completa para servidores web no ESP32*