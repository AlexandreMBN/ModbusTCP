# 📡 IMPLEMENTAÇÃO MQTT - SONDA LAMBDA ESP32

## ✅ **IMPLEMENTAÇÃO COMPLETA**

O sistema agora envia automaticamente os dados da sonda lambda para brokers MQTT públicos como **HiveMQ**.

---

## 🌐 **CONFIGURAÇÃO MQTT**

### **Broker Configurado:**
- **URL:** `mqtt://broker.hivemq.com`
- **Porta:** `1883`
- **Cliente ID:** `ESP32_SondaLambda`
- **QoS:** `1` (garantia de entrega)
- **Reconexão:** Automática

### **WebSocket (Opcional):**
- **URL:** `ws://broker.hivemq.com:8000/mqtt`

---

## 📊 **TÓPICOS MQTT PUBLICADOS**

| **Tópico** | **Conteúdo** | **Formato** | **Exemplo** |
|-------------|--------------|-------------|-------------|
| `esp32/sonda_lambda/heat` | Valor do aquecedor | Número | `118` |
| `esp32/sonda_lambda/lambda` | Sensor lambda | Número | `139` |
| `esp32/sonda_lambda/error` | Erro de controle | Número | `52` |
| `esp32/sonda_lambda/o2` | % Oxigênio | Número | `257` |
| `esp32/sonda_lambda/output` | Saída PID | Número | `0` |
| `esp32/sonda_lambda/status` | Status do dispositivo | String | `online`/`offline` |
| `esp32/sonda_lambda/data` | **Todos os dados** | **JSON** | Ver abaixo |

### **Exemplo do JSON Completo:**
```json
{
  "heat": 118,
  "lambda": 139,
  "error": 52,
  "o2": 257,
  "output": 0,
  "timestamp": 385110,
  "device_id": "ESP32_SondaLambda"
}
```

---

## 🔧 **COMO TESTAR**

### **1. Monitor MQTT Online (Fácil):**
- Acesse: [HiveMQ WebSocket Client](http://www.hivemq.com/demos/websocket-client/)
- **Host:** `broker.hivemq.com`
- **Port:** `8000`
- **Path:** `/mqtt`
- **Inscreva-se em:** `esp32/sonda_lambda/+` (todos os tópicos)

### **2. Linha de Comando (Mosquitto):**
```bash
# Instalar mosquitto clients
sudo apt-get install mosquitto-clients

# Monitorar todos os tópicos
mosquitto_sub -h broker.hivemq.com -t "esp32/sonda_lambda/+"

# Monitorar apenas dados JSON
mosquitto_sub -h broker.hivemq.com -t "esp32/sonda_lambda/data"
```

### **3. Node-RED:**
```javascript
// Node MQTT In
// Server: broker.hivemq.com:1883
// Topic: esp32/sonda_lambda/+
```

### **4. Python Cliente:**
```python
import paho.mqtt.client as mqtt
import json

def on_message(client, userdata, message):
    topic = message.topic
    payload = message.payload.decode()
    
    if topic.endswith('/data'):
        data = json.loads(payload)
        print(f"Sonda Lambda - Heat: {data['heat']}, Lambda: {data['lambda']}, O2: {data['o2']}")
    else:
        print(f"{topic}: {payload}")

client = mqtt.Client()
client.on_message = on_message
client.connect("broker.hivemq.com", 1883, 60)
client.subscribe("esp32/sonda_lambda/+")
client.loop_forever()
```

---

## 📈 **FREQUÊNCIA DE DADOS**

- **Logs UART:** A cada 1 segundo
- **MQTT:** A cada 1 segundo (junto com logs)
- **Dados:** Valores em tempo real da sonda lambda

---

## 🔄 **FLUXO DE DADOS**

```
MCT Task (sonda) → Fila MQTT → MQTT Client Task → Broker HiveMQ → Seus aplicativos
     ↓                 ↓              ↓                ↓              ↓
  A cada 1s         Queue         Publica        Distribui      Recebe dados
```

---

## ⚙️ **CONFIGURAÇÃO AVANÇADA**

### **Alterar Broker:**
No arquivo `include/mqtt_client_task.h`:
```c
#define MQTT_BROKER_URL "mqtt://seu-broker.com"
#define MQTT_PORT 1883
```

### **Alterar Tópicos:**
```c
#define MQTT_TOPIC_BASE "meu_dispositivo/sensores"
```

### **Credenciais (se necessário):**
```c
// Na inicialização MQTT
strcpy(mqtt_config.username, "usuario");
strcpy(mqtt_config.password, "senha");
```

---

## 🚨 **STATUS E MONITORAMENTO**

### **Logs do Sistema:**
```
I (1234) MQTT_CLIENT: MQTT Conectado ao broker: mqtt://broker.hivemq.com
I (1235) MAIN: MQTT Task: RODANDO
I (1236) MAIN: MQTT Status: CONECTADO
```

### **Verificar Conexão:**
- **Via logs UART:** `MQTT Status: CONECTADO/DESCONECTADO`
- **Via status do sistema:** A cada 30 segundos
- **Tópico status:** `esp32/sonda_lambda/status` = `online`

---

## 🛠️ **INTEGRAÇÃO COM APLICAÇÕES**

### **Grafana + InfluxDB:**
1. Configure telegraf para ler do MQTT
2. Armazene no InfluxDB
3. Visualize no Grafana

### **Home Assistant:**
```yaml
sensor:
  - platform: mqtt
    name: "Sonda Lambda Heat"
    state_topic: "esp32/sonda_lambda/heat"
    unit_of_measurement: "ADC"
  
  - platform: mqtt
    name: "Oxigênio %"
    state_topic: "esp32/sonda_lambda/o2"
    unit_of_measurement: "%"
```

### **Aplicativo Web Simples:**
Use qualquer cliente MQTT JavaScript (como MQTT.js) para criar dashboards web em tempo real.

---

## 🎯 **RESULTADO**

✅ **Dados da sonda lambda agora são enviados automaticamente via MQTT**  
✅ **Acesso remoto aos dados em tempo real**  
✅ **Integração fácil com qualquer sistema**  
✅ **Reconexão automática em caso de falha**  
✅ **Logs detalhados para debug**

**Pronto para usar!** 🚀