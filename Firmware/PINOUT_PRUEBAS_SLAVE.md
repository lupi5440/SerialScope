# 📌 Guía de Conexiones — ESP32 Slave (Pruebas)

Este dispositivo actúa como el **receptor del canal UART**. Recibe mensajes del Maestro y los notifica a la web vía **Bluetooth (BLE)**.

---

## 🚦 LEDs Indicadores
Conecta cada LED con una resistencia de **220 Ω a 330 Ω**.

| Indicador | Pin GPIO | Color LED | Color Cable Sugerido | Comportamiento |
|:---|:---:|:---|:---|:---|
| **Modo UART** | **13** 🔴 | 🔴 Rojo | 🔴 Rojo | Parpadea según el baudrate (1=9600...5=115200). |
| **Estado BLE** | **2** ⚪ | ⚪ Blanco | ⚪ Blanco | Parpadea buscando conexión; fijo al conectar. |
| **Status OK** | **15** 🟢 | 🟢 Verde | 🟢 Verde | Parpadea al recibir PING o datos del Maestro. |

---

## 🧪 Conexiones de Protocolo

### 📟 UART (Módulo MAX3232)

| Función ESP32 | Pin GPIO | Color Cable Sugerido | Conexión Física |
|:---|:---:|:---|:---|
| **RX** (Escucha) | **16** 🟣 | 🟣 Morado | TX del módulo MAX3232 Slave |
| **TX** (Envía) | **17** 🟤 | 🟤 Café | RX del módulo MAX3232 Slave |

> **Nota Técnica:** Los baudrates (9600 a 115200) se sincronizan automáticamente desde la interfaz web.

---

## ⚠️ Notas de Implementación (Slave)
- **Corte de Mensajes (Chunking):** Se implementó un sistema de **fragmentación BLE (20 bytes)** para asegurar que los mensajes largos del Maestro no se pierdan debido a los límites del Bluetooth.
- **Lectura UART Fluida:** El código procesa los datos carácter por carácter en lugar de esperar una línea completa, lo que garantiza una respuesta instantánea.
- **GND:** Es obligatorio unir el pin **GND** de este ESP32 con el GND del Maestro y del módulo MAX3232.

## 🛠️ Solución de Problemas (Troubleshooting)
- **¿LED Blanco parpadea infinito?** Revisa que el Bluetooth de tu PC esté encendido y que el dispositivo aparezca como "SerialScope Pruebas Slave".
- **¿No llegan datos al monitor serial?** Verifica que la velocidad del monitor esté a **115200 baudios**.
- **¿Datos invertidos?** Si recibes basura, intercambia los cables RX y TX en los pines 16 y 17.
