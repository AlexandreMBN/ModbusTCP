# Configuração Manual de Rede - ESP32 WiFi Manager

## Funcionalidades Adicionadas

Este projeto agora inclui funcionalidades para configuração manual de rede (IP estático e DHCP) após a conexão com uma rede WiFi.

### Novas Funcionalidades

1. **Configuração de IP Estático**
   - Permite definir IP, máscara de sub-rede, gateway e DNS manualmente
   - Configuração é salva no sistema de arquivos SPIFFS
   - Aplicação imediata das configurações de rede

2. **Ativação de DHCP**
   - Permite voltar ao modo DHCP automático
   - Remove configurações de IP estático

3. **Interface Web Melhorada**
   - Botão "Configurar Rede" na página de informações quando conectado
   - Página dedicada para configuração de rede
   - Validação de campos obrigatórios
   - Feedback visual de sucesso/erro

### Como Usar

#### 1. Conectar à Rede WiFi
- Primeiro, conecte-se a uma rede WiFi usando a interface existente
- Após a conexão bem-sucedida, vá para a página "Info"

#### 2. Acessar Configuração de Rede
- Na página de informações, clique no botão **"Configurar Rede"** (laranja)
- Faça login se necessário (usuário: `adm`, senha: `adm`)

#### 3. Configurar IP Estático
- Preencha os campos:
  - **Endereço IP**: IP desejado para o ESP32 (ex: 192.168.1.100)
  - **Máscara de Sub-rede**: Máscara da rede (ex: 255.255.255.0)
  - **Gateway**: IP do roteador (ex: 192.168.1.1)
  - **DNS**: Servidor DNS (ex: 8.8.8.8)
- Clique em **"Aplicar IP Estático"**

#### 4. Ativar DHCP
- Para voltar ao modo DHCP automático, clique em **"Ativar DHCP"**

### Arquivos Modificados

#### `include/storage.h`
- Adicionadas funções para salvar/carregar configurações de rede

#### `src/storage.c`
- Implementação das funções de armazenamento de configurações de rede
- Salva configurações em `/spiffs/network_config.json`

#### `include/wifi_manager.h`
- Adicionadas funções para aplicar configurações de rede

#### `src/wifi_manager.c`
- `wifi_apply_static_ip()`: Aplica configuração de IP estático
- `wifi_apply_dhcp()`: Ativa modo DHCP

#### `src/webserver.c`
- `network_config_get_handler()`: Página de configuração de rede
- `network_config_post_handler()`: Processa configuração de IP estático
- `apply_dhcp_handler()`: Processa ativação de DHCP
- Modificada página de informações para incluir botão de configuração

### Estrutura de Dados

#### Arquivo de Configuração de Rede
```json
{
  "ip": "192.168.1.100",
  "mask": "255.255.255.0",
  "gateway": "192.168.1.1",
  "dns": "8.8.8.8"
}
```

### Fluxo de Funcionamento

1. **Conexão WiFi**: Usuário conecta à rede WiFi
2. **Acesso à Configuração**: Clica em "Configurar Rede" na página de informações
3. **Configuração**: Define IP estático ou ativa DHCP
4. **Aplicação**: Sistema aplica as configurações imediatamente
5. **Persistência**: Configurações são salvas para uso futuro

### Validações

- Campos IP, máscara e gateway são obrigatórios para IP estático
- Validação de formato de endereços IP
- Verificação de conexão WiFi antes de permitir configuração
- Autenticação requerida para acesso às configurações

### Logs e Debug

O sistema registra logs detalhados para:
- Aplicação de configurações de rede
- Erros de validação
- Sucesso/falha das operações
- Carregamento de configurações salvas

### Compatibilidade

- Funciona com ESP-IDF framework
- Compatível com ESP32
- Requer sistema de arquivos SPIFFS montado
- Funciona em conjunto com o sistema WiFi existente 