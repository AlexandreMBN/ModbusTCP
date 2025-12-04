# Sistema de Controle de Logs via Flags (main_config.json)

## Visão Geral

Este sistema permite **controlar a exibição de logs específicos** do ESP32 editando apenas o arquivo `main_config.json` no SPIFFS, **sem necessidade de recompilar o código**. Com isso, você pode:

- ✅ **Reduzir poluição do console** ocultando logs verbosos
- ✅ **Facilitar debug** habilitando apenas logs relevantes
- ✅ **Melhorar performance** reduzindo overhead de I/O no serial
- ✅ **Configurar remotamente** via upload de arquivo JSON

## Problema Resolvido

Antes desta implementação, logs como estes apareciam continuamente no console:

```
I (105330) MAIN: Flags carregadas: 0x00000000, Prioritários: 0x00000000, Total: 0
I (105800) SONDA_CONTROL: 🔍 Tentando enviar O2=0% (fila tem 50 msgs)
W (105800) QUEUE_MANAGER: ⚠️ Fila O2 cheia! Dados perdidos: 0%
W (105800) SONDA_CONTROL: ❌ Fila O2 FALHOU: ESP_ERR_TIMEOUT (usando fallback)
I (106030) MODBUS_TCP: 📝 HOLDING READ: ADDR=1000, SIZE=3
I (106320) SONDA_CONTROL: Valor do heat: 0
I (106320) SONDA_CONTROL: Valor do erro: 0
I (106330) SONDA_CONTROL: Valor do lambda: 109
I (106340) SONDA_CONTROL: Valor do O2: 0
I (106340) SONDA_CONTROL: Valor do u: 0
```

**Agora você controla quais desses logs deseja ver!**

## Localização do Arquivo

```
data/config/main_config.json
```

## Estrutura do Arquivo (Atualizada)

```json
{
    "rtu_enabled": false,
    "tcp_enabled": true,
    "AP_enabled": true,
    "sta_enabled": true,
    "log_main_flags": false,
    "log_sonda_queue": false,
    "log_sonda_values": false,
    "log_modbus_tcp": false
}
```

## Novas Flags de Controle de Logs

| Flag | Tipo | Descrição | Logs Controlados | Padrão |
|------|------|-----------|------------------|--------|
| `log_main_flags` | boolean | Controla logs da máquina de estados | `"Flags carregadas: 0x..."` | `false` |
| `log_sonda_queue` | boolean | Controla logs de operações de fila da sonda | `"🔍 Tentando enviar O2..."`, `"❌ Fila O2 FALHOU..."`, `"⚠️ Fila O2 cheia!"` | `false` |
| `log_sonda_values` | boolean | Controla logs de valores do sensor | `"Valor do heat..."`, `"Valor do erro..."`, `"Valor do lambda..."`, `"Valor do O2..."`, `"Valor do u..."` | `false` |
| `log_modbus_tcp` | boolean | Controla logs de operações Modbus TCP | `"📝 HOLDING READ: ADDR=..."` | `false` |

### Flags Existentes (Controle de Módulos)

| Flag | Tipo | Descrição | Padrão |
|------|------|-----------|--------|
| `rtu_enabled` | boolean | Habilita/desabilita Modbus RTU | `false` |
| `tcp_enabled` | boolean | Habilita/desabilita Modbus TCP | `false` |
| `AP_enabled` | boolean | Habilita/desabilita modo Access Point | `true` |
| `sta_enabled` | boolean | Habilita/desabilita modo Station | `true` |

## Como Usar

### 1. Editar o Arquivo

Edite `data/config/main_config.json` para habilitar/desabilitar logs:

#### Exemplo 1: Habilitar Todos os Logs (Debug Completo)

```json
{
    "rtu_enabled": false,
    "tcp_enabled": true,
    "AP_enabled": true,
    "sta_enabled": true,
    "log_main_flags": true,
    "log_sonda_queue": true,
    "log_sonda_values": true,
    "log_modbus_tcp": true
}
```

**Resultado:** Console mostrará todos os logs de debug.

#### Exemplo 2: Desabilitar Todos os Logs (Produção Limpa)

```json
{
    "rtu_enabled": false,
    "tcp_enabled": true,
    "AP_enabled": true,
    "sta_enabled": true,
    "log_main_flags": false,
    "log_sonda_queue": false,
    "log_sonda_values": false,
    "log_modbus_tcp": false
}
```

**Resultado:** Console limpo, sem logs verbosos (somente erros críticos).

#### Exemplo 3: Debug Apenas da Sonda (Focado)

```json
{
    "rtu_enabled": false,
    "tcp_enabled": true,
    "AP_enabled": true,
    "sta_enabled": true,
    "log_main_flags": false,
    "log_sonda_queue": true,
    "log_sonda_values": true,
    "log_modbus_tcp": false
}
```

**Resultado:** Mostra apenas logs relacionados à sonda lambda (valores e fila).

#### Exemplo 4: Debug Apenas do Modbus TCP

```json
{
    "rtu_enabled": false,
    "tcp_enabled": true,
    "AP_enabled": true,
    "sta_enabled": true,
    "log_main_flags": false,
    "log_sonda_queue": false,
    "log_sonda_values": false,
    "log_modbus_tcp": true
}
```

**Resultado:** Mostra apenas operações de leitura/escrita Modbus TCP.

### 2. Upload para SPIFFS

```bash
# PlatformIO CLI
pio run --target uploadfs --environment esp32dev

# Ou via PlatformIO IDE
# Clique em "Upload Filesystem Image" na barra lateral
```

### 3. Reiniciar ESP32

O sistema carregará as novas configurações automaticamente no boot.

## Detalhamento dos Logs Controlados

### 1. `log_main_flags` - Logs da Máquina de Estados

**Arquivo:** `src/main.c`  
**Função:** `load_all_pending_events()`

**Logs controlados:**
```
I (105330) MAIN: Flags carregadas: 0x00000000, Prioritários: 0x00000000, Total: 0
```

**Quando habilitar:**
- Debug de eventos da máquina de estados
- Verificação de fluxo de eventos do sistema
- Diagnóstico de timeout ou travamentos

**Quando desabilitar:**
- Produção (log muito frequente)
- Console limpo para outros logs
- Economia de banda do serial

---

### 2. `log_sonda_queue` - Logs de Fila da Sonda

**Arquivos:**
- `src/oxygen_sensor_task.c`
- `src/queue_manager.c`

**Logs controlados:**
```
I (105800) SONDA_CONTROL: 🔍 Tentando enviar O2=0% (fila tem 50 msgs)
W (105800) QUEUE_MANAGER: ⚠️ Fila O2 cheia! Dados perdidos: 0%
W (105800) SONDA_CONTROL: ❌ Fila O2 FALHOU: ESP_ERR_TIMEOUT (usando fallback)
I (105800) SONDA_CONTROL: ✅ Dados O2 enviados via fila: 0% (a cada 500ms)
```

**Quando habilitar:**
- Debug de problemas de comunicação entre tasks
- Verificação de overflow da fila
- Diagnóstico de perda de dados
- Otimização do tamanho da fila

**Quando desabilitar:**
- Fila funcionando corretamente
- Console muito poluído
- Produção estável

---

### 3. `log_sonda_values` - Logs de Valores do Sensor

**Arquivo:** `src/oxygen_sensor_task.c`

**Logs controlados:**
```
I (106320) SONDA_CONTROL: Valor do heat: 0
I (106320) SONDA_CONTROL: Valor do erro: 0
I (106330) SONDA_CONTROL: Valor do lambda: 109
I (106340) SONDA_CONTROL: Valor do O2: 0
I (106340) SONDA_CONTROL: Valor do u: 0
I (106350) SONDA_CONTROL: ___________________________________________________________
```

**Quando habilitar:**
- Calibração da sonda lambda
- Debug de leituras incorretas
- Verificação do controlador PID
- Análise de comportamento térmico
- Desenvolvimento de novos algoritmos

**Quando desabilitar:**
- Sistema calibrado e funcionando
- Logs muito frequentes (a cada segundo)
- Produção (dados acessíveis via Modbus/MQTT)

---

### 4. `log_modbus_tcp` - Logs de Operações Modbus TCP

**Arquivo:** `src/modbus_tcp_slave_task.c`

**Logs controlados:**
```
I (106030) MODBUS_TCP: 📝 HOLDING READ: ADDR=1000, SIZE=3
```

**Quando habilitar:**
- Debug de comunicação Modbus TCP
- Verificação de endereços acessados
- Diagnóstico de cliente Modbus
- Auditoria de acessos aos registradores

**Quando desabilitar:**
- Comunicação Modbus funcionando
- Muitas leituras por segundo
- Console poluído com operações normais
- Produção estável

## Como o Sistema Funciona Internamente

### 1. Carregamento das Flags (Boot)

```c
// No app_main() (main.c)
esp_err_t config_ret = load_main_config_flags();
```

A função `load_main_config_flags()` lê o arquivo JSON e preenche a estrutura global `FLAGS`:

```c
typedef struct {
    bool rtu_enabled;
    bool tcp_enabled;
    bool AP_enabled;
    bool sta_enabled;
    bool log_main_flags;    // ← NOVO
    bool log_sonda_queue;   // ← NOVO
    bool log_sonda_values;  // ← NOVO
    bool log_modbus_tcp;    // ← NOVO
} main_flags_t;

extern main_flags_t FLAGS;  // Acessível globalmente
```

### 2. Verificação Antes de Logar

Exemplo em `main.c`:

```c
// Antes (sempre loga)
ESP_LOGI(TAG, "Flags carregadas: 0x%08lX...", ...);

// Depois (verifica flag)
if (FLAGS.log_main_flags) {
    ESP_LOGI(TAG, "Flags carregadas: 0x%08lX...", ...);
}
```

Exemplo em `oxygen_sensor_task.c`:

```c
// Logs de fila
if (FLAGS.log_sonda_queue) {
    ESP_LOGI(TAG, "🔍 Tentando enviar O2=%d%%...", o2Percent);
}

// Logs de valores
if (FLAGS.log_sonda_values) {
    ESP_LOGI(TAG, "Valor do heat: %d", heatValue);
    ESP_LOGI(TAG, "Valor do erro: %d", erro);
    // ...
}
```

### 3. Valores Padrão (Segurança)

Se o arquivo não existir ou houver erro de parsing, o sistema usa valores seguros:

```c
FLAGS.log_main_flags = false;
FLAGS.log_sonda_queue = false;
FLAGS.log_sonda_values = false;
FLAGS.log_modbus_tcp = false;
```

**Por quê `false` por padrão?**
- Console limpo em produção
- Melhor performance (menos I/O serial)
- Debug habilitado explicitamente quando necessário

## Exemplo Completo de Uso

### Cenário: Debug de Problema na Fila O2

**Sintoma:** Dados de O2 não chegam no MQTT

**Passo 1:** Habilitar logs de fila da sonda

```json
{
    "rtu_enabled": false,
    "tcp_enabled": true,
    "AP_enabled": true,
    "sta_enabled": true,
    "log_main_flags": false,
    "log_sonda_queue": true,  ← Habilitado
    "log_sonda_values": false,
    "log_modbus_tcp": false
}
```

**Passo 2:** Upload e reiniciar

```bash
pio run --target uploadfs --environment esp32dev
# Pressionar botão RESET no ESP32
```

**Passo 3:** Monitorar logs

```bash
pio device monitor
```

**Saída esperada:**
```
I (105800) SONDA_CONTROL: 🔍 Tentando enviar O2=0% (fila tem 50 msgs)
W (105800) QUEUE_MANAGER: ⚠️ Fila O2 cheia! Dados perdidos: 0%
W (105800) SONDA_CONTROL: ❌ Fila O2 FALHOU: ESP_ERR_TIMEOUT
```

**Diagnóstico:** Fila está cheia! Consumidor (Modbus task) não está processando rápido.

**Solução:** Aumentar tamanho da fila ou aumentar prioridade da task consumidora.

**Passo 4:** Após resolver, desabilitar logs

```json
{
    ...
    "log_sonda_queue": false  ← Desabilitado novamente
}
```

## Boas Práticas

### ✅ Desenvolvimento

```json
{
    "log_main_flags": true,
    "log_sonda_queue": true,
    "log_sonda_values": true,
    "log_modbus_tcp": true
}
```

**Vantagens:**
- Visibilidade completa do sistema
- Facilita debug rápido
- Permite análise de fluxo

### ✅ Testes de Integração

```json
{
    "log_main_flags": false,
    "log_sonda_queue": true,  ← Apenas comunicação entre tasks
    "log_sonda_values": false,
    "log_modbus_tcp": true    ← Apenas Modbus
}
```

**Vantagens:**
- Foco em pontos de integração
- Console menos poluído
- Ainda detecta problemas críticos

### ✅ Produção

```json
{
    "log_main_flags": false,
    "log_sonda_queue": false,
    "log_sonda_values": false,
    "log_modbus_tcp": false
}
```

**Vantagens:**
- Console limpo
- Melhor performance
- Apenas logs críticos (erros)
- Dados acessíveis via Modbus/MQTT

## Troubleshooting

### Problema: Flags não têm efeito

**Sintoma:** Logs continuam aparecendo mesmo com flags `false`

**Possíveis causas:**
1. ❌ Não fez upload do filesystem (`uploadfs`)
2. ❌ Arquivo JSON com sintaxe incorreta
3. ❌ Código não recompilado após mudanças

**Solução:**
```bash
# 1. Valide JSON (https://jsonlint.com/)

# 2. Force recompilação e upload completo
pio run --target upload --environment esp32dev
pio run --target uploadfs --environment esp32dev

# 3. Verifique logs de carregamento
pio device monitor
# Procure por: "✅ Configurações carregadas com sucesso:"
```

### Problema: Arquivo não encontrado

**Sintoma:** Log mostra "⚠️ Arquivo main_config.json não encontrado"

**Solução:**
```bash
# Verifique se arquivo existe em data/config/
ls data/config/main_config.json

# Se não existir, crie:
echo '{"rtu_enabled":false,"tcp_enabled":true,"AP_enabled":true,"sta_enabled":true,"log_main_flags":false,"log_sonda_queue":false,"log_sonda_values":false,"log_modbus_tcp":false}' > data/config/main_config.json

# Upload:
pio run --target uploadfs --environment esp32dev
```

## Monitoramento via Logs

### Logs Importantes de Carregamento

| Mensagem | Significado |
|----------|-------------|
| `✅ Configurações carregadas com sucesso` | JSON lido e parseado OK |
| `├─ log_main_flags: ✓ SIM` | Flag habilitada |
| `├─ log_sonda_queue: ✗ NÃO` | Flag desabilitada |
| `⚠️ Usando configuração padrão` | Arquivo não encontrado, usando fallback |

### Exemplo de Log de Boot

```
I (1234) MAIN: 📄 Carregando configurações de main_config.json...
I (1235) MAIN: 🔍 Tentando abrir: /spiffs/config/main_config.json
I (1236) MAIN: ✅ Arquivo encontrado em /spiffs/config/main_config.json
I (1250) MAIN: ✅ Configurações carregadas com sucesso:
I (1251) MAIN:    ├─ AP_enabled:  ✓ SIM
I (1252) MAIN:    ├─ sta_enabled: ✓ SIM
I (1253) MAIN:    ├─ rtu_enabled: ✗ NÃO
I (1254) MAIN:    ├─ tcp_enabled: ✓ SIM
I (1255) MAIN:    ├─ log_main_flags: ✗ NÃO       ← Logs MAIN desabilitados
I (1256) MAIN:    ├─ log_sonda_queue: ✗ NÃO      ← Logs FILA desabilitados
I (1257) MAIN:    ├─ log_sonda_values: ✗ NÃO     ← Logs VALORES desabilitados
I (1258) MAIN:    └─ log_modbus_tcp: ✗ NÃO       ← Logs MODBUS desabilitados
```

## Arquivos Modificados

### Novos Arquivos

- `include/main_config_flags.h` - Header com definição da estrutura de flags

### Arquivos Modificados

- `src/main.c` - Estrutura de flags e carregamento do JSON
- `src/oxygen_sensor_task.c` - Controle de logs de fila e valores
- `src/modbus_tcp_slave_task.c` - Controle de logs Modbus TCP
- `src/queue_manager.c` - Controle de warnings de fila cheia
- `data/config/main_config.json` - Novas flags no arquivo de configuração

## Melhorias Futuras

- [ ] Interface web para editar flags de log em tempo real
- [ ] Controle de log level por módulo (ERROR, WARN, INFO, DEBUG)
- [ ] Rotação automática de logs para análise posterior
- [ ] Estatísticas de logs (quantos warnings, erros, etc.)
- [ ] Export de logs via HTTP para análise offline

---

**Data:** 24 de novembro de 2025  
**Status:** ✅ Implementado e testado  
**Versão:** 1.0  
**Autor:** Sistema de controle de logs via flags
