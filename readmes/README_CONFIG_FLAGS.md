# Sistema de Configuração por Flags (main_config.json)

## Visão Geral

O sistema permite **habilitar/desabilitar recursos** do ESP32 editando apenas o arquivo `main_config.json` no SPIFFS, sem necessidade de recompilar o código.

## Localização do Arquivo

```
data/config/main_config.json
```

## Estrutura do Arquivo

```json
{
    "rtu_enabled": false,
    "tcp_enabled": false,
    "AP_enabled": true,
    "sta_enabled": true
}
```

## Flags Disponíveis

| Flag | Tipo | Descrição | Padrão | Efeito |
|------|------|-----------|--------|--------|
| `rtu_enabled` | boolean | Habilita/desabilita Modbus RTU (Serial) | `false` | Cria/não cria task `modbus_slave_task` |
| `tcp_enabled` | boolean | Habilita/desabilita Modbus TCP (Ethernet/WiFi) | `false` | Inicializa/não inicializa `modbus_tcp_slave_init()` |
| `AP_enabled` | boolean | Habilita/desabilita modo Access Point | `true` | AP fica ativo/inativo |
| `sta_enabled` | boolean | Habilita/desabilita modo Station (cliente WiFi) | `true` | STA conecta/não conecta automaticamente |

## Como Usar

### 1. Editar o Arquivo

Edite `data/config/main_config.json` com os valores desejados:

```json
{
    "rtu_enabled": true,    // ✅ Habilita Modbus RTU
    "tcp_enabled": false,   // ❌ Desabilita Modbus TCP
    "AP_enabled": true,     // ✅ Mantém AP ativo
    "sta_enabled": false    // ❌ Desabilita STA (não conecta ao roteador)
}
```

### 2. Upload para SPIFFS

```bash
# PlatformIO
pio run --target uploadfs --environment esp32dev

# Ou via PlatformIO IDE
# Clique em "Upload Filesystem Image" na barra lateral
```

### 3. Reiniciar ESP32

O sistema carregará as novas configurações automaticamente no boot.

## Cenários de Uso Comuns

### Cenário 1: Apenas AP (sem conexão ao roteador)

**Caso de uso:** Sistema isolado, acesso apenas via AP

```json
{
    "rtu_enabled": false,
    "tcp_enabled": false,
    "AP_enabled": true,
    "sta_enabled": false    // ← Desabilita STA
}
```

**Resultado:**
- ✅ AP ativo em `192.168.4.1`
- ❌ Não tenta conectar ao roteador
- ✅ WebServer acessível via AP
- ✅ Economia de energia (STA desligado)

### Cenário 2: Modo Dual (AP + STA)

**Caso de uso:** Máxima flexibilidade, acesso local e remoto

```json
{
    "rtu_enabled": true,
    "tcp_enabled": true,
    "AP_enabled": true,
    "sta_enabled": true     // ← STA ativo
}
```

**Resultado:**
- ✅ AP ativo em `192.168.4.1`
- ✅ STA conecta ao roteador (se credenciais salvas)
- ✅ Modbus RTU ativo
- ✅ Modbus TCP ativo

### Cenário 3: Apenas STA (produção)

**Caso de uso:** Ambiente industrial, conexão fixa ao roteador

```json
{
    "rtu_enabled": true,
    "tcp_enabled": true,
    "AP_enabled": false,     // ← AP desabilitado (economiza energia)
    "sta_enabled": true
}
```

**Resultado:**
- ❌ AP desligado
- ✅ STA conecta ao roteador
- ✅ Modbus RTU e TCP ativos
- ⚠️ **CUIDADO:** Se STA falhar, sistema fica inacessível!

### Cenário 4: Debug/Desenvolvimento

**Caso de uso:** Testes com sensores, sem comunicação

```json
{
    "rtu_enabled": false,    // ← Desabilita Modbus para testes isolados
    "tcp_enabled": false,
    "AP_enabled": true,
    "sta_enabled": true
}
```

## Como o Sistema Funciona

### 1. Carregamento no Boot

```c
// No app_main() (main.c)
esp_err_t config_ret = load_main_config_flags();
if (config_ret != ESP_OK) {
    ESP_LOGW(TAG, "⚠️ Usando configuração padrão");
}
```

### 2. Aplicação das Flags

#### WiFi (main.c - wifi_init_task)

```c
// Configura flag no wifi_manager
wifi_set_sta_enabled(FLAGS.sta_enabled);

if (FLAGS.AP_enabled && FLAGS.sta_enabled) {
    ESP_LOGI(TAG, "🚀 Iniciando WiFi em modo DUAL (AP + STA)...");
    start_wifi_ap();
} else if (FLAGS.AP_enabled) {
    ESP_LOGI(TAG, "🚀 Iniciando WiFi em modo AP APENAS...");
    start_wifi_ap();
}
```

#### Modbus (main.c - STATE_TASKS_START)

```c
// Modbus RTU (apenas se habilitado)
if (FLAGS.rtu_enabled) {
    ESP_LOGI(TAG, "🔧 Iniciando Modbus RTU...");
    xTaskCreate(modbus_slave_task, "Modbus RTU", 4096, NULL, 4, &task_handles.modbus_rtu_task_handle);
} else {
    ESP_LOGI(TAG, "⏭️  Modbus RTU desabilitado - pulando");
}

// Modbus TCP (apenas se habilitado)
if (FLAGS.tcp_enabled) {
    ESP_LOGI(TAG, "🔧 Inicializando Modbus TCP...");
    esp_err_t tcp_ret = modbus_tcp_slave_init();
}
```

### 3. Logs no Console

Quando `sta_enabled=false`:

```
📄 Carregando configurações de main_config.json...
✅ Configurações carregadas com sucesso:
   ├─ AP_enabled:  ✓ SIM
   ├─ sta_enabled: ✗ NÃO        ← STA desabilitado
   ├─ rtu_enabled: ✗ NÃO
   └─ tcp_enabled: ✗ NÃO

🔧 STA DESABILITADO via flag de configuração

╔═══════════════════════════════════════════════════════════════╗
║      *** STA DESABILITADO VIA CONFIGURAÇÃO (FLAG) ***        ║
╠═══════════════════════════════════════════════════════════════╣
║  AP ativo em: 192.168.4.1                                    ║
║  AP SSID: ESP32-MCT-01                                       ║
║  STA: DESABILITADO (sta_enabled=false no main_config.json)  ║
║  Para habilitar: mude sta_enabled=true e reinicie            ║
╚═══════════════════════════════════════════════════════════════╝
```

## Segurança e Valores Padrão

### Valores Padrão (se arquivo não existe ou falha no parsing)

```c
FLAGS.rtu_enabled = false;
FLAGS.tcp_enabled = false;
FLAGS.AP_enabled = true;   // ✅ AP sempre ativo por padrão (segurança)
FLAGS.sta_enabled = false;
```

**Por que `AP_enabled=true` por padrão?**
- Garante que o sistema sempre seja acessível
- Evita "brick" se configuração falhar
- Permite recuperação via interface web

### Validação de Configuração

```c
// Não permite desabilitar AP e STA simultaneamente
if (!FLAGS.AP_enabled && !FLAGS.sta_enabled) {
    ESP_LOGE(TAG, "❌ ERRO: AP e STA desabilitados! Sistema inacessível!");
    // Sistema entra em estado de erro
}
```

## Exemplo Completo de Teste

### Teste 1: Desabilitar STA

1. **Edite `data/config/main_config.json`:**
   ```json
   {
       "rtu_enabled": false,
       "tcp_enabled": false,
       "AP_enabled": true,
       "sta_enabled": false
   }
   ```

2. **Upload para SPIFFS:**
   ```bash
   pio run --target uploadfs --environment esp32dev
   ```

3. **Monitore logs:**
   ```bash
   pio device monitor
   ```

4. **Verifique comportamento:**
   - ✅ AP deve iniciar normalmente
   - ✅ STA NÃO deve tentar conectar (mesmo com credenciais salvas)
   - ✅ Log deve mostrar "STA DESABILITADO VIA CONFIGURAÇÃO"
   - ✅ WebServer acessível em `http://192.168.4.1`

### Teste 2: Habilitar apenas Modbus RTU

1. **Edite o arquivo:**
   ```json
   {
       "rtu_enabled": true,
       "tcp_enabled": false,
       "AP_enabled": true,
       "sta_enabled": true
   }
   ```

2. **Upload e reinicie**

3. **Verifique logs:**
   ```
   📋 Configuração Modbus: RTU=✓, TCP=✗
   🔧 Iniciando Modbus RTU...
   ✅ Modbus RTU task criada
   ⏭️  Modbus TCP desabilitado - pulando
   ```

## Troubleshooting

### Problema: Flags não são aplicadas

**Sintoma:** Mudanças no `main_config.json` não têm efeito

**Possíveis causas:**
1. ❌ Não fez upload do filesystem (`uploadfs`)
2. ❌ Arquivo JSON com sintaxe incorreta
3. ❌ SPIFFS não montado corretamente

**Solução:**
```bash
# 1. Valide JSON online (https://jsonlint.com/)
# 2. Force upload do filesystem
pio run --target uploadfs --environment esp32dev

# 3. Verifique logs de carregamento
pio device monitor
# Procure por: "📄 Carregando configurações de main_config.json..."
```

### Problema: Sistema inacessível após desabilitar AP

**Sintoma:** Não consegue mais acessar ESP32

**Causa:** `AP_enabled=false` e STA falhou em conectar

**Solução:**
```bash
# Opção 1: Factory reset via GPIO (se implementado)
# Pressione botão de reset por 3 segundos

# Opção 2: Re-flash completo
pio run --target upload --environment esp32dev
pio run --target uploadfs --environment esp32dev
```

### Problema: STA ainda tenta conectar com `sta_enabled=false`

**Sintoma:** Logs mostram tentativas de conexão STA

**Causa:** Código não está usando versão atualizada

**Solução:**
```bash
# Recompile e faça upload completo
pio run --target upload --environment esp32dev
```

## Monitoramento via Logs

### Logs Importantes

| Mensagem | Significado |
|----------|-------------|
| `✅ Configurações carregadas com sucesso` | JSON lido e parseado OK |
| `sta_enabled: ✓ SIM` | STA será ativado |
| `sta_enabled: ✗ NÃO` | STA NÃO será ativado |
| `🔧 STA DESABILITADO via flag de configuração` | Flag aplicada com sucesso |
| `⏭️ STA desabilitado via flag (sta_enabled=false)` | STA_START ignorado conforme esperado |
| `⚠️ Usando configuração padrão` | Arquivo não encontrado, usando fallback |

## Checklist de Verificação

- [ ] Arquivo `main_config.json` existe em `data/config/`
- [ ] JSON tem sintaxe válida (use linter)
- [ ] Upload de filesystem (`uploadfs`) foi executado
- [ ] ESP32 foi reiniciado após upload
- [ ] Logs mostram "Configurações carregadas com sucesso"
- [ ] Comportamento esperado conforme flags definidas

## Arquivos Relacionados

- `data/config/main_config.json` - Arquivo de configuração
- `src/main.c` - Carregamento e aplicação das flags
- `src/wifi_manager.c` - Implementação do controle de STA
- `include/wifi_manager.h` - Protótipos das funções de controle

## Melhorias Futuras

- [ ] Interface web para editar `main_config.json`
- [ ] Validação de JSON na interface web
- [ ] Backup automático de configurações
- [ ] Reset para valores padrão via web
- [ ] Logs de histórico de mudanças de configuração

---

**Data:** 21 de novembro de 2025  
**Status:** ✅ Implementado e testado  
**Versão:** 1.0
