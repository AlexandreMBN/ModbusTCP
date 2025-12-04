# SPIFFS File Manager

Uma biblioteca completa e robusta para gerenciamento de arquivos SPIFFS e servidor web estático no ESP32. Projetada para facilitar a criação de interfaces web com cache inteligente, processamento de templates e integração perfeita com ESP-IDF.

## 🌟 Características Principais

- **Cache Inteligente**: Sistema de cache em memória com LRU automático
- **Detecção de MIME Types**: Suporte automático para HTML, CSS, JS, imagens e tipos customizados
- **Sistema de Templates**: Processamento de placeholders com substituição dinâmica
- **Handlers HTTP Pré-configurados**: Registros automáticos para diferentes tipos de arquivo
- **Preprocessors Customizáveis**: Processe arquivos antes de servi-los
- **Estatísticas Detalhadas**: Monitore uso, cache hits/misses e performance
- **Headers de Segurança**: Configuração automática de headers HTTP seguros
- **Flexibilidade Total**: Configuração para desenvolvimento e produção

## 📦 Instalação

### PlatformIO
```bash
# Copie a pasta lib/spiffs_file_manager para seu projeto
# A biblioteca será detectada automaticamente
```

### ESP-IDF
```bash
# Adicione como componente no seu projeto
# Inclua no CMakeLists.txt principal:
set(EXTRA_COMPONENT_DIRS "lib/spiffs_file_manager")
```

## 🚀 Uso Rápido

### Inicialização Básica

```c
#include "spiffs_file_manager.h"

void app_main() {
    // Configuração padrão
    spiffs_manager_config_t config;
    spiffs_manager_get_default_config(&config);
    
    // Customizar se necessário
    config.enable_cache = true;
    config.enable_development_headers = true;
    
    // Inicializar
    ESP_ERROR_CHECK(spiffs_manager_init(&config));
    
    // Criar servidor HTTP
    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &server_config));
    
    // Registrar handlers automáticos
    ESP_ERROR_CHECK(spiffs_manager_register_default_handlers(server));
    
    ESP_LOGI("APP", "Servidor rodando em http://192.168.4.1/");
}
```

### Carregamento Direto de Arquivos

```c
char *content = NULL;
size_t content_length = 0;

spiffs_manager_result_t result = spiffs_manager_load_file("html/index.html", &content, &content_length);
if (result == SPIFFS_MANAGER_OK) {
    printf("Arquivo carregado: %d bytes\\n", content_length);
    // Usar content...
    free(content);
} else {
    printf("Erro: %s\\n", spiffs_manager_get_error_string(result));
}
```

### Processamento de Templates

```c
const char *template_html = "<h1>{{TITLE}}</h1><p>Status: {{STATUS}}</p>";

spiffs_template_var_t variables[] = {
    {"TITLE", "Meu ESP32"},
    {"STATUS", "Online"}
};

char *processed_content = NULL;
spiffs_manager_result_t result = spiffs_manager_process_template(
    template_html, variables, 2, &processed_content);

if (result == SPIFFS_MANAGER_OK) {
    printf("HTML processado: %s\\n", processed_content);
    free(processed_content);
}
```

### Handler HTTP Customizado

```c
esp_err_t status_handler(httpd_req_t *req) {
    // Obter estatísticas
    uint32_t files_served, cache_hits, cache_misses;
    size_t total_bytes;
    
    spiffs_manager_get_stats(&files_served, &cache_hits, &cache_misses, &total_bytes);
    
    // Criar resposta JSON
    char json[256];
    snprintf(json, sizeof(json), 
        "{\"files_served\":%lu,\"cache_hits\":%lu,\"total_bytes\":%u}",
        files_served, cache_hits, total_bytes);
    
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, strlen(json));
}

// Registrar handler customizado
spiffs_manager_register_custom_handler(server, "/api/status", HTTP_GET, status_handler);
```

## ⚙️ Configuração

### Configuração Completa

```c
spiffs_manager_config_t config = {
    .base_path = "/spiffs",                    // Caminho SPIFFS
    .default_index = "index.html",             // Arquivo padrão
    .enable_cache = true,                      // Cache habilitado
    .enable_compression = false,               // Compressão (futuro)
    .enable_development_headers = true,        // Headers de dev
    .max_file_size = 64 * 1024,               // 64KB máximo
    .max_open_files = 10                       // Arquivos abertos
};
```

### MIME Types Customizados

```c
// Registrar novos tipos
spiffs_manager_register_mime_type(".log", "text/plain");
spiffs_manager_register_mime_type(".config", "application/json");
```

### Preprocessors Customizados

```c
esp_err_t my_preprocessor(const char *filepath, char **content, size_t *content_length) {
    // Processar conteúdo antes de servir
    // Por exemplo: minificar, comprimir, substituir tokens
    return ESP_OK;
}

// Registrar preprocessor
spiffs_manager_register_preprocessor(".js", my_preprocessor);
```

## 📁 Estrutura de Arquivos Esperada

```
data/
├── html/
│   ├── index.html      # Página principal
│   ├── config.html     # Páginas adicionais
│   └── status.html     
├── css/
│   └── styles.css      # Folhas de estilo
├── js/
│   └── script.js       # JavaScript
└── assets/
    ├── logo.png        # Imagens
    └── favicon.ico
```

## 🎯 Handlers Automáticos

A biblioteca registra automaticamente os seguintes handlers:

| Padrão URI | Descrição | Exemplo |
|------------|-----------|---------|
| `/` | Página inicial (index.html) | `http://ip/` |
| `/css/*` | Arquivos CSS | `http://ip/css/styles.css` |
| `/js/*` | Arquivos JavaScript | `http://ip/js/script.js` |
| `/html/*` | Páginas HTML | `http://ip/html/config.html` |
| `/assets/*` | Recursos estáticos | `http://ip/assets/logo.png` |

## 📊 Monitoramento e Estatísticas

```c
uint32_t files_served, cache_hits, cache_misses;
size_t total_bytes;

spiffs_manager_get_stats(&files_served, &cache_hits, &cache_misses, &total_bytes);

printf("Arquivos servidos: %lu\\n", files_served);
printf("Cache hits: %lu\\n", cache_hits);
printf("Taxa de acerto: %.2f%%\\n", 
    (float)cache_hits / (cache_hits + cache_misses) * 100.0);
```

## 🔧 Utilitários

### Verificação de Arquivos
```c
if (spiffs_manager_file_exists("config.json")) {
    size_t file_size;
    spiffs_manager_get_file_size("config.json", &file_size);
    printf("Arquivo existe: %d bytes\\n", file_size);
}
```

### Limpeza de Cache
```c
// Limpar cache manualmente
spiffs_manager_clear_cache();

// Remontar SPIFFS (limpa cache automaticamente)
spiffs_manager_remount_spiffs();
```

### Páginas de Erro Customizadas
```c
esp_err_t my_handler(httpd_req_t *req) {
    if (/* erro */) {
        return spiffs_manager_send_404(req, "Página não encontrada!");
    }
    
    if (/* erro interno */) {
        return spiffs_manager_send_500(req, "Erro no processamento");
    }
    
    return ESP_OK;
}
```

## 🎨 Templates Avançados

### Carregamento e Processamento
```c
spiffs_template_var_t vars[] = {
    {"DEVICE_NAME", "ESP32-WebServer"},
    {"IP_ADDRESS", "192.168.4.1"},
    {"UPTIME", "00:15:30"},
    {"FREE_MEMORY", "45KB"}
};

char *result = NULL;
spiffs_manager_load_and_process_template("html/status.html", vars, 4, &result);
```

### Template HTML Exemplo
```html
<!DOCTYPE html>
<html>
<head>
    <title>{{DEVICE_NAME}} - Status</title>
</head>
<body>
    <h1>{{DEVICE_NAME}}</h1>
    <p>IP: {{IP_ADDRESS}}</p>
    <p>Uptime: {{UPTIME}}</p>
    <p>Memória livre: {{FREE_MEMORY}}</p>
</body>
</html>
```

## 🔒 Segurança e Headers

A biblioteca configura automaticamente headers de segurança:

- `X-Content-Type-Options: nosniff`
- `X-Frame-Options: DENY`
- `Cache-Control` (configurável para dev/prod)

## 🚀 Performance

- **Memória Base**: ~8KB RAM
- **Cache Configurável**: Até 20 arquivos por padrão
- **Tamanho Máximo**: 64KB por arquivo (configurável)
- **Múltiplos Handlers**: Suporte a handlers simultâneos

## 🛠️ Desenvolvimento vs Produção

### Modo Desenvolvimento
```c
config.enable_development_headers = true;  // No-cache headers
config.enable_cache = true;                // Cache para testes
```

### Modo Produção
```c
config.enable_development_headers = false; // Cache headers
config.enable_cache = true;                // Cache otimizado
```

## 📚 API Completa

### Inicialização
- `spiffs_manager_get_default_config()`
- `spiffs_manager_init()`
- `spiffs_manager_deinit()`
- `spiffs_manager_is_initialized()`

### Carregamento de Arquivos
- `spiffs_manager_load_file()`
- `spiffs_manager_file_exists()`
- `spiffs_manager_get_file_size()`

### Templates
- `spiffs_manager_process_template()`
- `spiffs_manager_load_and_process_template()`

### Handlers HTTP
- `spiffs_manager_register_default_handlers()`
- `spiffs_manager_register_custom_handler()`
- `spiffs_manager_static_handler()`
- `spiffs_manager_template_handler()`

### Utilitários
- `spiffs_manager_get_mime_type()`
- `spiffs_manager_register_mime_type()`
- `spiffs_manager_set_http_headers()`
- `spiffs_manager_clear_cache()`
- `spiffs_manager_get_stats()`

## 🤝 Integração com Outras Bibliotecas

Esta biblioteca se integra perfeitamente com:
- **AP Manager**: Para configuração de rede
- **WiFi Manager**: Para conectividade
- **Config Manager**: Para persistência de configurações
- **MQTT Client**: Para comunicação IoT

## 📄 Licença

MIT License - Livre para uso comercial e pessoal.

## 🆘 Suporte

Para dúvidas, bugs ou sugestões:
1. Verifique os exemplos incluídos
2. Revise a documentação da API
3. Consulte os logs do ESP-IDF
4. Abra uma issue no repositório

---

**Desenvolvido para ESP32 com ❤️ - Torne suas interfaces web mais rápidas e profissionais!**