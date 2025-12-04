# Sistema de Autenticação - Web Server MCT-01

## Credenciais Padrão

O sistema possui dois níveis de usuário com credenciais padrão:

### Usuário Básico (adm)
- **Usuário:** `adm`
- **Senha:** `adm`
- **Nível:** `WSM_USER_LEVEL_BASIC` (1)
- **Permissões:** Acesso básico, visualização de dados

### Usuário Administrador (root)
- **Usuário:** `root`
- **Senha:** `root`
- **Nível:** `WSM_USER_LEVEL_ADMIN` (2)
- **Permissões:** Acesso completo, configuração do sistema

## Onde as Credenciais São Definidas

### Arquivo Principal
**Localização:** `lib/web_server_manager/src/web_server_manager.c`

```c
// Linhas 168-171
// Credenciais padrão
strncpy(g_wsm_ctx.basic_user, "adm", sizeof(g_wsm_ctx.basic_user) - 1);
strncpy(g_wsm_ctx.basic_pass, "adm", sizeof(g_wsm_ctx.basic_pass) - 1);
strncpy(g_wsm_ctx.admin_user, "root", sizeof(g_wsm_ctx.admin_user) - 1);
strncpy(g_wsm_ctx.admin_pass, "root", sizeof(g_wsm_ctx.admin_pass) - 1);
```

### Função de Inicialização
**Função:** `wsm_init()`  
**Arquivo:** `lib/web_server_manager/src/web_server_manager.c` (linha 124)

Esta função é chamada durante a inicialização do sistema e define as credenciais padrão para ambos os usuários.

## Como Alterar as Credenciais

### Método 1: Alterar no Código (Permanente)

Edite o arquivo `lib/web_server_manager/src/web_server_manager.c`:

```c
// Para alterar o usuário básico
strncpy(g_wsm_ctx.basic_user, "seu_usuario", sizeof(g_wsm_ctx.basic_user) - 1);
strncpy(g_wsm_ctx.basic_pass, "sua_senha", sizeof(g_wsm_ctx.basic_pass) - 1);

// Para alterar o administrador
strncpy(g_wsm_ctx.admin_user, "seu_admin", sizeof(g_wsm_ctx.admin_user) - 1);
strncpy(g_wsm_ctx.admin_pass, "sua_senha_admin", sizeof(g_wsm_ctx.admin_pass) - 1);
```

**Após a alteração:**
```powershell
pio run --target upload --environment esp32dev
```

### Método 2: Usar a Função API (Runtime)

Use a função `wsm_set_auth_credentials()` em tempo de execução:

```c
esp_err_t wsm_set_auth_credentials(
    const char *basic_user,  // Usuário básico
    const char *basic_pass,  // Senha básica
    const char *admin_user,  // Usuário admin
    const char *admin_pass   // Senha admin
);
```

**Exemplo:**
```c
wsm_set_auth_credentials("adm", "nova_senha", "root", "nova_senha_root");
```

## Sistema de Sessões

### Geração de Sessão
- **ID da Sessão:** 128 bits aleatórios (UUID-like)
- **Cookie:** `WSM_SESSION_ID`
- **Timeout:** Definido por `WSM_SESSION_TIMEOUT_MS`
- **Máximo de Sessões:** 8 simultâneas

### Cookie de Sessão
```
WSM_SESSION_ID=<session_id>; Path=/; HttpOnly; Max-Age=<timeout>
```

### Verificação de Autenticação
**Função:** `wsm_check_auth()`  
**Arquivo:** `lib/web_server_manager/src/wsm_auth.c` (linha 182)

```c
esp_err_t wsm_check_auth(httpd_req_t *req, wsm_user_level_t required_level);
```

## Processo de Login

### 1. Usuário Acessa `/login`
Página de login é exibida

### 2. Credenciais São Enviadas via POST
```http
POST /login HTTP/1.1
Content-Type: application/x-www-form-urlencoded

username=root&password=root
```

### 3. Validação das Credenciais
**Função:** `wsm_process_login()`  
**Arquivo:** `lib/web_server_manager/src/wsm_auth.c` (linha 286)

```c
esp_err_t wsm_process_login(const char *username, const char *password, 
                            wsm_user_level_t *level);
```

**Lógica:**
```c
// Verifica usuário básico
if (strcmp(username, g_wsm_ctx.basic_user) == 0 && 
    strcmp(password, g_wsm_ctx.basic_pass) == 0) {
    *level = WSM_USER_LEVEL_BASIC;
    return ESP_OK;
}

// Verifica administrador
if (strcmp(username, g_wsm_ctx.admin_user) == 0 && 
    strcmp(password, g_wsm_ctx.admin_pass) == 0) {
    *level = WSM_USER_LEVEL_ADMIN;
    return ESP_OK;
}
```

### 4. Criação da Sessão
Se as credenciais forem válidas:
- **Session ID** é gerado
- **Cookie** é criado e enviado ao navegador
- **Sessão** é armazenada no array de sessões

### 5. Redirecionamento
```http
HTTP/1.1 302 Found
Location: /admin
Set-Cookie: WSM_SESSION_ID=<session_id>; Path=/; HttpOnly; Max-Age=3600
```

## Níveis de Acesso

| Nível | Valor | Nome | Descrição |
|-------|-------|------|-----------|
| Nenhum | 0 | `WSM_USER_LEVEL_NONE` | Sem autenticação |
| Básico | 1 | `WSM_USER_LEVEL_BASIC` | Usuário padrão (adm) |
| Admin | 2 | `WSM_USER_LEVEL_ADMIN` | Administrador (root) |

## Permissões por Nível

### Usuário Básico (adm)
- Visualizar dados dos sensores  
- Ver configurações básicas  
- Acessar página de informações  
- Não pode modificar configurações  
- Não pode fazer upload/download de arquivos  
- Não pode editar registradores Modbus completos  

### Administrador (root)
- **TODAS** as permissões do usuário básico  
- Modificar todas as configurações  
- Upload de arquivos JSON  
- Download de configurações  
- Editar todos os registradores Modbus  
- Configurar MQTT/TLS  
- Gerenciar tarefas do sistema  

## Endpoints Protegidos

### Requer Admin (root)
```c
/api/config/upload          // Upload de configurações JSON
/api/config/download/*      // Download de configurações
/config                     // Modificação de configs
/modbus_tcp_config          // Config Modbus TCP
/mqtt_config                // Config MQTT
```

### Requer Básico (adm ou root)
```c
/admin                      // Painel de controle
/info                       // Informações do sistema
/wifi                       // Status WiFi
```

### Público (sem autenticação)
```c
/login                      // Página de login
/logout                     // Logout
/css/*                      // Arquivos CSS
/js/*                       // Arquivos JavaScript
```

## Logout

### Processo de Logout
**Endpoint:** `/logout`  
**Função:** `wsm_logout_session()`  
**Arquivo:** `lib/web_server_manager/src/wsm_auth.c` (linha 317)

**O que acontece:**
1. Sessão é marcada como inválida
2. Cookie é limpo (Max-Age=0)
3. Redirecionamento para `/login`

```c
esp_err_t wsm_logout_session(httpd_req_t *req) {
    // Invalida a sessão
    session->valid = false;
    
    // Limpa o cookie
    httpd_resp_set_hdr(req, "Set-Cookie", 
        "WSM_SESSION_ID=; Path=/; HttpOnly; Max-Age=0");
    
    return ESP_OK;
}
```

## Timeout e Limpeza de Sessões

### Timeout de Sessão
- **Padrão:** `WSM_SESSION_TIMEOUT_MS` (definido em `web_server_manager.h`)
- **Inatividade:** Sessão expira após período sem acesso

### Limpeza Automática
**Função:** `wsm_session_cleanup_expired()`  
**Chamada:** A cada verificação de autenticação

```c
static void wsm_session_cleanup_expired(void) {
    uint64_t now = esp_timer_get_time() / 1000000;
    
    for (int i = 0; i < 8; i++) {
        wsm_session_t *session = &g_wsm_ctx.sessions[i];
        if (session->valid && 
            now - session->last_access > (WSM_SESSION_TIMEOUT_MS / 1000)) {
            session->valid = false;
        }
    }
}
```

## Segurança

### Recursos de Segurança Implementados
- **HttpOnly Cookie** - Previne acesso via JavaScript  
- **Session Timeout** - Expira sessões inativas  
- **Cleanup Automático** - Remove sessões expiradas  
- **Limite de Sessões** - Máximo 8 sessões simultâneas  
- **Verificação por Endpoint** - Cada rota verifica permissões  

### Recomendações Adicionais
- **Alterar senhas padrão** em produção  
- **Habilitar HTTPS/TLS** para conexões seguras  
- **Implementar rate limiting** para prevenir brute force  
- **Armazenar senhas criptografadas** (futuro)  

## Estrutura de Dados

### Contexto de Autenticação
```c
typedef struct {
    char basic_user[32];     // Usuário básico
    char basic_pass[64];     // Senha básica
    char admin_user[32];     // Usuário admin
    char admin_pass[64];     // Senha admin
    wsm_session_t sessions[8]; // Array de sessões
    // ... outros campos
} wsm_context_t;
```

### Estrutura de Sessão
```c
typedef struct {
    char session_id[64];      // ID único da sessão
    wsm_user_level_t user_level; // Nível de acesso
    uint64_t created_at;      // Timestamp de criação
    uint64_t last_access;     // Último acesso
    bool valid;               // Sessão válida?
} wsm_session_t;
```

## Testando Autenticação

### Via cURL

```bash
# Login como root
curl -X POST http://192.168.4.1/login \
  -d "username=root&password=root" \
  -c cookies.txt

# Acessar endpoint protegido
curl http://192.168.4.1/admin \
  -b cookies.txt

# Logout
curl http://192.168.4.1/logout \
  -b cookies.txt
```

### Via Navegador
1. Abra http://192.168.4.1/login
2. Digite **root** / **root**
3. Pressione Enter ou clique em Login
4. Você será redirecionado para `/admin`

## Arquivos Relacionados

| Arquivo | Descrição |
|---------|-----------|
| `lib/web_server_manager/src/web_server_manager.c` | Inicialização e credenciais padrão |
| `lib/web_server_manager/src/wsm_auth.c` | Sistema completo de autenticação |
| `lib/web_server_manager/include/web_server_manager.h` | Definições e APIs públicas |
| `data/html/login.html` | Página de login |
| `data/html/admin.html` | Painel administrativo |

---

**Data:** 25 de novembro de 2025  
**Versão:** MCT-01 v1.0  
**Sistema:** Web Server Manager com Autenticação de Sessão
