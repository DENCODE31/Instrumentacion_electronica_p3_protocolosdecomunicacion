![License](https://img.shields.io/badge/License-MIT-blue.svg)
![Status](https://img.shields.io/badge/Status-Development-yellow.svg)
![ESP32](https://img.shields.io/badge/MCU-ESP32-red.svg)
![Made in Colombia](https://img.shields.io/badge/Made%20in-Colombia-yellow.svg)

# Práctica 3 — Protocolos de Comunicación con ESP32

**Universidad Nacional de Colombia · Instrumentación Electrónica · 7° Grupo**

Repositorio de prácticas de laboratorio enfocado en la implementación de protocolos de comunicación cableados e inalámbricos sobre microcontroladores ESP32, usando el framework **ESP-IDF** con **FreeRTOS**. Cada carpeta es un proyecto independiente y compilable, que aborda desde comunicación serial básica hasta conectividad IoT sobre WiFi y Bluetooth.

---

## Estructura del repositorio

```
03_practica/
├── 01_UART/
│   └── adivinar_numero_comunicacion_UART/   # Juego interactivo por terminal serial
├── 02_I2C/
│   └── digital_clock_and_potenciometer/     # Reloj digital en OLED + lectura ADC
└── 04_Comunicacion_inalambrica/
    ├── 01_bluetooth/                         # Servidor GATT BLE con NimBLE
    ├── 02_wifi_basico/                       # Servidor HTTP sobre WiFi
    └── 03_MQTT/                              # Cliente MQTT sobre WiFi
```

---

## Protocolos implementados

### UART — Universal Asynchronous Receiver-Transmitter

**Proyecto:** `01_UART/adivinar_numero_comunicacion_UART`

Juego interactivo de adivinar un número enviado completamente a través de la interfaz UART del ESP32. El usuario define un rango enviando dos enteros por la terminal serial y luego intenta adivinar el número secreto generado aleatoriamente (`esp_random()`). El sistema responde con pistas hasta que el número es adivinado.

| Parámetro | Valor |
|-----------|-------|
| Puerto | `UART_NUM_0` |
| Baud rate | 115200 |
| Frame | 8N1 (8 bits datos, sin paridad, 1 stop bit) |
| Control de flujo | Deshabilitado |
| Buffer RX | 2048 bytes |

**Conceptos clave:** configuración del driver UART en ESP-IDF, lectura no bloqueante con timeout, parsing de cadenas recibidas, generación de números pseudoaleatorios con hardware RNG.

---

### I2C — Inter-Integrated Circuit

**Proyecto:** `02_I2C/digital_clock_and_potenciometer`

Reloj digital funcional que muestra la hora actual y el valor de un potenciómetro en tiempo real sobre una pantalla OLED **SSD1306 de 128×64 píxeles**. El bus I2C conecta el ESP32 con la pantalla. Paralelamente, el ADC de 12 bits lee el potenciómetro y tanto la hora como el valor analógico se imprimen también por UART.

| Parámetro | Valor |
|-----------|-------|
| Bus | I2C maestro |
| SDA / SCL (ESP32) | GPIO21 / GPIO22 |
| SDA / SCL (ESP32-C6) | GPIO5 / GPIO6 |
| Display | SSD1306 128×64 |
| ADC | 12 bits, atenuación 12 dB |
| Canal ADC (ESP32) | ADC1_CH6 → GPIO34 |
| Canal ADC (ESP32-C6) | ADC1_CH0 → GPIO0 |

**Conceptos clave:** inicialización I2C maestro, driver SSD1306 vía librería `esp-idf-ssd1306`, renderizado de texto en pantalla, ADC oneshot con `esp_adc`, temporización sin bloqueo usando `esp_timer`.

---

### BLE — Bluetooth Low Energy (GATT/GAP)

**Proyecto:** `04_Comunicacion_inalambrica/01_bluetooth`

Servidor GATT BLE implementado con el stack **NimBLE** de ESP-IDF. Expone dos servicios Bluetooth estándar:

- **Heart Rate Service** (`UUID: 0x180D`) — simula una medición de frecuencia cardíaca. Soporta lectura directa e indicaciones periódicas al cliente suscrito.
- **Automation IO Service** (`UUID: 0x1815`) — permite controlar el LED integrado del ESP32 de forma remota mediante escrituras BLE desde una app como *nRF Connect*.

| Capa | Detalle |
|------|---------|
| Stack | NimBLE (Apache Mynewt) |
| Rol | Peripheral / GATT Server |
| Modo | Advertising → Connection |
| Acceso HR | Read + Indicate |
| Acceso LED | Write only |
| MTU | Negociado automáticamente |

**Conceptos clave:** inicialización NimBLE, tabla de servicios GATT (`ble_gatt_svc_def`), manejo de eventos GAP (`BLE_GAP_EVENT_*`), suscripción a indicaciones, callbacks de acceso a características, tarea FreeRTOS para actualización periódica.

---

### WiFi + HTTP — Servidor HTTP embebido

**Proyecto:** `04_Comunicacion_inalambrica/02_wifi_basico`

Servidor HTTP corriendo directamente en el ESP32 sobre WiFi usando el componente `esp_http_server` de ESP-IDF. Demuestra el manejo de múltiples URIs con distintos métodos HTTP.

| Endpoint | Método | Descripción |
|----------|--------|-------------|
| `/hello` | GET | Retorna `"Hello World!"` |
| `/echo` | POST | Eco del cuerpo recibido |
| `/ctrl` | PUT | Habilita / deshabilita handlers en tiempo de ejecución |
| `/sse` | GET | Server-Sent Events — envía un mensaje al cliente cada segundo |

| Parámetro | Valor |
|-----------|-------|
| Estándar WiFi | 802.11 b/g/n |
| Puerto HTTP | 80 |
| Stack TCP/IP | LwIP |
| Protocolo | HTTP/1.1 |

**Conceptos clave:** conexión WiFi con `example_connect`, registro de URI handlers, lectura y escritura del request/response HTTP, SSE (streaming de eventos del servidor al cliente).

---

### MQTT — Message Queuing Telemetry Transport

**Proyecto:** `04_Comunicacion_inalambrica/03_MQTT`

Cliente MQTT corriendo sobre WiFi que se conecta a un broker público, demuestra publicación y suscripción a tópicos con QoS 0 y QoS 1, y maneja el ciclo completo de eventos del protocolo.

| Parámetro | Valor |
|-----------|-------|
| Versión del protocolo | MQTT v3.1.1 |
| Transporte | TCP (port 1883) |
| QoS soportado | QoS 0, QoS 1 |
| Librería | `esp-mqtt` (ESP-IDF) |
| Tópicos de demo | `/topic/qos0`, `/topic/qos1` |

**Flujo de operación:**
1. Conexión WiFi → obtención de IP
2. Conexión al broker MQTT (`MQTT_EVENT_CONNECTED`)
3. Publish en `/topic/qos1`
4. Subscribe a `/topic/qos0` y `/topic/qos1`
5. Unsubscribe de `/topic/qos1`
6. Recepción y log de mensajes entrantes (`MQTT_EVENT_DATA`)

**Conceptos clave:** máquina de estados del cliente MQTT, manejo del event loop con `esp_event`, QoS y confirmación de entrega de mensajes, broker URI configurable por `menuconfig`.

---

## Stack tecnológico

| Herramienta | Uso |
|-------------|-----|
| ESP-IDF v5.x | Framework principal |
| FreeRTOS | Multitarea y temporización |
| NimBLE | Stack BLE |
| LwIP | Stack TCP/IP |
| esp-mqtt | Cliente MQTT |
| esp-idf-ssd1306 | Driver OLED |
| VS Code + ESP-IDF Extension | Entorno de desarrollo |

---

## Compilar y flashear

```bash
# Configurar target
idf.py set-target esp32      # o esp32c6, esp32s3, etc.

# Configurar parámetros (WiFi SSID, contraseña, broker MQTT, etc.)
idf.py menuconfig

# Compilar, flashear y monitorear
idf.py -p COM<X> flash monitor
```

Para salir del monitor: `Ctrl + ]`

---

## Autor

**Yeison Dénnir Termal Cuastumal**  
Ingeniería Electrónica — Universidad Nacional de Colombia  2026
[GitHub](https://github.com/DENCODE31)


---

## Estado del proyecto

**COMPLETADO** — Semestre 2026-1 cerrado.

- Fecha cierre: 2026-06-18
- Materia: INSTRUMENTACION
- Entrega: aprobada
- Estado código: funcional, archivado
