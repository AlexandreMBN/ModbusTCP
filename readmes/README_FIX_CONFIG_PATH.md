# Correção: main_config.json não sendo carregado

## Problema Identificado

O arquivo `main_config.json` estava sendo criado em `data/config/main_config.json` mas o código tentava ler de um caminho **incorreto**.

### Causa Raiz

1. **SPIFFS montado em:** `/spiffs`
2. **Código buscava em:** `/data/config/main_config.json` ❌
3. **Caminho correto:** `/spiffs/config/main_config.json` ✅

### Log do Erro

```
W (668) MAIN: ⚠️ Arquivo main_config.json não encontrado, usando valores padrão
W (688) MAIN: ⚠️ Usando configuração padrão (AP ativo, outros recursos desabilitados)
```

## Correções Implementadas

### 1. Corrigido o Caminho de Leitura

**Antes:**
```c
FILE *f = fopen("/data/config/main_config.json", "r");
```

**Depois:**
```c
FILE *f = fopen("/spiffs/config/main_config.json", "r");
```

### 2. Adicionada Montagem Explícita do SPIFFS

```c
static esp_err_t load_main_config_flags(void) {
    // Garante que SPIFFS esteja montado
    static bool spiffs_mounted = false;
    if (!spiffs_mounted) {
        esp_vfs_spiffs_conf_t conf = {
            .base_path = "/spiffs",
            .partition_label = NULL,
            .max_files = 10,
            .format_if_mount_failed = true,
        };
        esp_err_t ret = esp_vfs_spiffs_register(&conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "❌ Falha ao montar SPIFFS: %s", esp_err_to_name(ret));
            return ESP_FAIL;
        }
        spiffs_mounted = true;
        ESP_LOGI(TAG, "✅ SPIFFS montado em /spiffs");
    }
    
    // ... resto do código
}
```

### 3. Logs Melhorados para Debug

```c
ESP_LOGI(TAG, "🔍 Tentando abrir: /spiffs/config/main_config.json");
FILE *f = fopen("/spiffs/config/main_config.json", "r");
if (f == NULL) {
    ESP_LOGW(TAG, "⚠️ Arquivo não encontrado em /spiffs/config/");
    
    // Tenta caminho alternativo
    f = fopen("/spiffs/main_config.json", "r");
    if (f == NULL) {
        ESP_LOGW(TAG, "⚠️ Também não encontrado em /spiffs/main_config.json");
        return ESP_FAIL;
    } else {
        ESP_LOGI(TAG, "✅ Arquivo encontrado em /spiffs/main_config.json");
    }
} else {
    ESP_LOGI(TAG, "✅ Arquivo encontrado em /spiffs/config/main_config.json");
}
```

## Estrutura Correta de Pastas

A estrutura no SPIFFS deve ficar assim:

```
/spiffs/
├── config/
│   ├── main_config.json       ← Configurações das flags
│   ├── ap_config.json          ← Configurações do AP
│   ├── sta_config.json         ← Credenciais WiFi STA
│   ├── network_config.json     ← IP estático
│   ├── rtu_config.json         ← Modbus RTU
│   └── modbus_tcp_config.json  ← Modbus TCP
├── html/
│   └── ... (arquivos HTML)
├── css/
│   └── styles.css
└── js/
    └── ... (arquivos JavaScript)
```

## Como Testar a Correção

### 1. Compile o Código Corrigido

```bash
pio run --target upload --environment esp32dev
```

### 2. Upload do Filesystem

```bash
pio run --target uploadfs --environment esp32dev
```

### 3. Monitore os Logs

```bash
pio device monitor
```

### 4. Verifique os Logs Esperados

**Logs de Sucesso:**
```
I (xxx) MAIN: 📄 Carregando configurações de main_config.json...
I (xxx) MAIN: ✅ SPIFFS montado em /spiffs
I (xxx) MAIN: 🔍 Tentando abrir: /spiffs/config/main_config.json
I (xxx) MAIN: ✅ Arquivo encontrado em /spiffs/config/main_config.json
I (xxx) MAIN: ✅ Configurações carregadas com sucesso:
I (xxx) MAIN:    ├─ AP_enabled:  ✓ SIM
I (xxx) MAIN:    ├─ sta_enabled: ✓ SIM        ← DEVE mostrar SIM agora!
I (xxx) MAIN:    ├─ rtu_enabled: ✗ NÃO
I (xxx) MAIN:    └─ tcp_enabled: ✗ NÃO
```

**Se STA estiver habilitado (`sta_enabled: true`):**
```
I (xxx) WIFI_MANAGER: ╔═══════════════════════════════════════════════════╗
I (xxx) WIFI_MANAGER: ║   *** CONFIGURAÇÃO WIFI ENCONTRADA ***           ║
I (xxx) WIFI_MANAGER: ║       ATIVANDO MODO DUAL (AP + STA)              ║
I (xxx) WIFI_MANAGER: ╚═══════════════════════════════════════════════════╝
I (xxx) WIFI_MANAGER: STA iniciado - verificando se deve conectar
I (xxx) WIFI_MANAGER: SSID configurado detectado: [SEU_SSID]
I (xxx) WIFI_MANAGER: Conexão STA iniciada automaticamente
```

## Troubleshooting

### Problema 1: Ainda mostra "Arquivo não encontrado"

**Possível causa:** Upload do filesystem não funcionou

**Solução:**
```bash
# 1. Limpe o build
pio run --target clean

# 2. Verifique partições
cat partitions.csv

# 3. Force upload do filesystem
pio run --target uploadfs --environment esp32dev

# 4. Monitore processo de upload
# Deve mostrar "Writing at 0x00..."
```

### Problema 2: SPIFFS não monta

**Possível causa:** Partição SPIFFS muito pequena ou corrompida

**Solução:**
```bash
# 1. Verifique partitions.csv
# Deve ter algo como:
# spiffs, data, spiffs, 0x290000, 0x170000,

# 2. Apague SPIFFS e recrie
pio run --target erase
pio run --target upload
pio run --target uploadfs
```

### Problema 3: JSON não é parseado

**Possível causa:** Arquivo JSON corrompido ou sintaxe inválida

**Solução:**
```bash
# 1. Valide JSON online
# Copie conteúdo de data/config/main_config.json
# Cole em https://jsonlint.com/

# 2. Verifique se não tem BOM ou caracteres especiais
# Use editor que mostra caracteres invisíveis
```

## 📊 Checklist de Verificação

- [ ] Código compilado com correções
- [ ] Upload de código executado (`upload`)
- [ ] Upload de filesystem executado (`uploadfs`)
- [ ] ESP32 reiniciado após uploads
- [ ] Log mostra "SPIFFS montado em /spiffs"
- [ ] Log mostra "Arquivo encontrado em /spiffs/config/main_config.json"
- [ ] Log mostra "sta_enabled: ✓ SIM" (se configurado como true)
- [ ] STA conecta automaticamente (se habilitado)

## 🎯 Comandos Completos para Aplicar Correção

Execute na ordem:

```bash
# 1. Compile e faça upload do código corrigido
pio run --target upload --environment esp32dev

# 2. Faça upload do filesystem
pio run --target uploadfs --environment esp32dev

# 3. Monitore os logs
pio device monitor

# 4. Aguarde inicialização e verifique logs
```

## Status

**Data:** 21 de novembro de 2025  
**Status:** ✅ Corrigido  
**Teste:** Pendente (aguardando recompilação e teste do usuário)

---

**IMPORTANTE:** Após aplicar estas correções, o sistema de flags via `main_config.json` funcionará corretamente!
