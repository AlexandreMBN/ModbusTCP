# Botão Físico de Reset - GPIO 4

## Descrição
Este projeto agora inclui suporte para um botão físico conectado na GPIO 4 que permite fazer o reset de fábrica da ESP32. O LED integrado na GPIO 2 fornece feedback visual durante o processo de reset.

## Como Conectar o Botão

### Hardware Necessário:
- 1 botão push-button (momentâneo)
- 1 resistor de 10kΩ (opcional, para pull-down externo)
- LED integrado na GPIO 2 (já presente na maioria das placas ESP32)

### Conexão:
```
ESP32 GPIO 4 ────┐
                 │
                [Botão]
                 │
                GND
```

**Nota:** O código já configura o pull-up interno da GPIO 4, então o resistor externo é opcional.

### Esquema Alternativo (com resistor):
```
ESP32 GPIO 4 ────┐
                 │
                [Botão]
                 │
                GND
                 │
                [10kΩ]
                 │
                GND
```

## Como Funciona

### Comportamento:
1. **Pressione e segure** o botão por **3 segundos**
2. **LED acende** imediatamente quando o botão é pressionado
3. O sistema detectará o pressionamento prolongado
4. **LED pisca rapidamente** (5 vezes) quando o tempo é atingido
5. Executará o reset de fábrica automaticamente
6. A ESP32 será reiniciada com configurações padrão

### Indicadores Visuais do LED:
- **LED apagado**: Sistema normal, botão não pressionado
- **LED aceso**: Botão pressionado, aguardando tempo mínimo
- **LED piscando**: Reset será executado em instantes
- **LED apagado**: Sistema reiniciado (após reset)

### Logs no Monitor Serial:
```
I (1234) MAIN: Sistema iniciado - Botão de reset na GPIO 4 (segure por 3 segundos)
I (1245) MAIN: Task do botão de reset iniciada
I (5000) MAIN: Botão de reset pressionado - segure por 3 segundos
I (8000) MAIN: Botão pressionado por tempo suficiente - executando reset de fábrica
I (8001) MAIN: Reset de fábrica acionado pelo botão físico!
```

### Proteções Implementadas:
- **Tempo mínimo**: O botão deve ficar pressionado por pelo menos 3 segundos
- **Pull-up interno**: Evita falsos acionamentos
- **Debounce**: Verificação a cada 100ms para estabilidade
- **Logs detalhados**: Para debug e monitoramento

## Configuração

### Alterar o Pino (se necessário):
No arquivo `src/main.c`, linha 12:
```c
#define RESET_BUTTON_GPIO GPIO_NUM_4  // Mude para outro pino se necessário
```

### Alterar o Pino do LED (se necessário):
No arquivo `src/main.c`, linha 16:
```c
#define RESET_LED_GPIO GPIO_NUM_2  // Mude para outro pino se necessário
```

### Alterar o Tempo de Pressionamento:
No arquivo `src/main.c`, linha 13:
```c
#define RESET_BUTTON_PRESS_TIME_MS 3000  // Tempo em milissegundos
```

## Teste

1. Compile e faça upload do código
2. Abra o monitor serial (115200 baud)
3. Pressione e segure o botão por 3 segundos
4. Observe os logs no monitor
5. A ESP32 deve reiniciar e voltar às configurações de fábrica

## Segurança

- O botão só funciona após o sistema estar completamente inicializado
- O tempo de 3 segundos evita resets acidentais
- Logs detalhados permitem monitorar o funcionamento
- A função de reset é idêntica à do webserver, garantindo consistência
- Proteções contra crashes implementadas (stack aumentado, delays de estabilização)

## Troubleshooting de Crashes

### Se o sistema apresentar "Guru Meditation Error":
1. **Reinicie a ESP32** - pode ser um problema temporário
2. **Verifique a conexão do botão** - curto-circuito pode causar instabilidade
3. **Monitore os logs** - identifique onde o crash ocorre
4. **Teste sem o botão** - confirme se o problema é do hardware ou software

### Logs de erro comuns:
- `LoadProhibited` / `StoreProhibited`: Problema de acesso à memória
- `Stack overflow`: Stack insuficiente para a task
- `Watchdog timeout`: Sistema travado por muito tempo

## Troubleshooting

### Botão não responde:
- Verifique se a conexão está correta
- Confirme se o botão está conectado entre GPIO 4 e GND
- Verifique se o LED acende quando pressiona o botão
- Verifique os logs no monitor serial

### LED não acende:
- Confirme se o LED está conectado na GPIO 2
- Verifique se o LED não está queimado
- Teste com outro LED se necessário

### Reset acidental:
- Aumente o tempo de `RESET_BUTTON_PRESS_TIME_MS`
- Verifique se não há curto-circuito no botão

### Logs não aparecem:
- Verifique se o monitor serial está configurado para 115200 baud
- Confirme se a GPIO 4 não está sendo usada por outro componente 