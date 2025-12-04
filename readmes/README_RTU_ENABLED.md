# Parâmetro `enabled` no RTU Config

## Visão Geral

Adicionado suporte ao parâmetro `enabled` no arquivo `rtu_config.json`, similar ao já existente no `modbus_tcp_config.json`. Isso permite habilitar/desabilitar o protocolo Modbus RTU de forma independente.

## Funcionalidades

### **Antes:**
```json
{
  "uart_port": 2,
  "baud_rate": 115200,
  "slave_address": 1,
  "data_bits": 8,
  "parity": 0,
  "stop_bits": 1
}
```

### **Depois:**
```json
{
  "enabled": true,
  "uart_port": 2,
  "baud_rate": 115200,
  "slave_address": 1,
  "data_bits": 8,
  "parity": 0,
  "stop_bits": 1
}
```

## Benefícios

✅ **Controle Individual**: Habilitar/desabilitar RTU sem afetar TCP  
✅ **Economia de Recursos**: Desabilitar RTU quando usar apenas TCP  
✅ **Consistência**: Mesma estrutura do `modbus_tcp_config.json`  
✅ **Compatibilidade**: Se `enabled` não existir no JSON, assume `true` (habilitado)

## Implementação

### **Arquivos Modificados:**

1. **`data/config/rtu_config.json`**
   - Adicionado campo `"enabled": true`

2. **`include/modbus_params.h`**
   - Declarada variável global `extern bool modbus_rtu_enabled;`

3. **`src/modbus_params.c`**
   - Definida variável global `bool modbus_rtu_enabled = true;`

4. **`src/config_manager.c`**
   - `save_rtu_config()`: Salva o valor de `modbus_rtu_enabled` no JSON
   - `load_rtu_config()`: Carrega o valor do JSON para `modbus_rtu_enabled`
   - Fallback para `true` se campo não existir (compatibilidade)

5. **`examples/rtu_config_example.json`**
   - Atualizado exemplo com campo `enabled`

## Como Usar

### **Variável Global:**
```c
#include "modbus_params.h"

// Verificar se RTU está habilitado
if (modbus_rtu_enabled) {
    // Inicializar Modbus RTU
    ESP_LOGI(TAG, "Modbus RTU habilitado, iniciando...");
} else {
    ESP_LOGI(TAG, "Modbus RTU desabilitado, pulando inicialização");
}
```

### **Modificar em Tempo de Execução:**
```c
// Desabilitar RTU
modbus_rtu_enabled = false;
save_rtu_config();  // Salvar no arquivo

// Habilitar RTU
modbus_rtu_enabled = true;
save_rtu_config();  // Salvar no arquivo
```

### **Carregar Configuração:**
```c
// Carrega do arquivo e atualiza modbus_rtu_enabled
load_rtu_config();

ESP_LOGI(TAG, "RTU Status: %s", modbus_rtu_enabled ? "HABILITADO" : "DESABILITADO");
```

## Compatibilidade

A implementação é **100% retrocompatível**:

- Se o arquivo `rtu_config.json` **não tiver** o campo `enabled`, o sistema assume `true` (habilitado)
- Arquivos antigos continuam funcionando normalmente
- Log de aviso é gerado quando o campo não é encontrado

## Próximos Passos Sugeridos

Para utilizar completamente essa funcionalidade, você pode:

1. **Modificar `modbus_slave_task.c`** para verificar `modbus_rtu_enabled` antes de inicializar:
   ```c
   void modbus_slave_task(void *param) {
       if (!modbus_rtu_enabled) {
           ESP_LOGI(TAG, "Modbus RTU desabilitado, task não iniciará");
           vTaskDelete(NULL);
           return;
       }
       // ... resto da inicialização RTU
   }
   ```

2. **Adicionar controle via WebServer** para habilitar/desabilitar RTU pela interface web

3. **Atualizar main.c** para condicionar a criação da task RTU:
   ```c
   if (modbus_rtu_enabled) {
       xTaskCreate(modbus_slave_task, "Modbus RTU", 4096, NULL, 4, &task_handles.modbus_rtu_task_handle);
   }
   ```

## Comparação RTU vs TCP

| Característica | RTU | TCP |
|---------------|-----|-----|
| **Campo `enabled`** | ✅ Sim (novo) | ✅ Sim |
| **Variável Global** | `modbus_rtu_enabled` | `modbus_tcp_config.enabled` |
| **Valor Padrão** | `true` | `false` |
| **Arquivo Config** | `/data/config/rtu_config.json` | `/data/config/modbus_tcp_config.json` |

## Exemplo de Log

```
I (12345) CONFIG_MANAGER: Carregando configuração RTU...
I (12350) CONFIG_MANAGER: ✅ RTU enabled carregado: HABILITADO
I (12355) CONFIG_MANAGER: ✅ Configuração RTU carregada do arquivo SPIFFS
```

Ou, se o campo não existir:
```
I (12345) CONFIG_MANAGER: Carregando configuração RTU...
W (12350) CONFIG_MANAGER: ⚠️ 'enabled' não encontrado no JSON, mantendo RTU habilitado (padrão)
```

---

**Data da Implementação**: 18 de novembro de 2025  
**Versão**: 1.0
