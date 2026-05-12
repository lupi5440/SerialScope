# Guía de Conexión Física — ESP32 Pruebas Slave

Este módulo actúa como el "Extremo Receptor" o "Destino" en las pruebas de comunicación. Su función principal es recibir datos por UART y reportarlos a la interfaz para verificar que el mensaje llegó íntegro.

## 💡 LEDs Indicadores (Slave)

| LED | GPIO | Función | Comportamiento |
|:---:|:---:|:---|:---|
| **⚪ Blanco** | **4** | **Estado BLE** | Parpadea buscando conexión. Fijo al vincular. |
| **🟢 Verde** | **13** | **PING OK** | Destella al recibir un comando PING desde la web. |
| **🔴 Rojo** | **14** | **Actividad UART** | Parpadea según la velocidad (Baudrate) configurada. |

---

## 🔌 Conexiones de Protocolo

### 📟 UART / RS232 (Conexión Destino)
Es el puerto que recibe los mensajes enviados por el Master o el Visualizador.

| Función | Pin GPIO | Color Sugerido | Nota Técnica |
|:---|:---:|:---|:---|
| **RX2** | **16** 🟣 | 🟣 Púrpura | Conectar al **TX** del emisor (Master o Sniffer). |
| **TX2** | **17** 🟤 | 🟤 Marrón | Conectar al **RX** del emisor (Master o Sniffer). |

> [!IMPORTANT]
> **REGLA DE ORO:** Recuerda siempre realizar la conexión cruzada (**RX ↔ TX**) y asegurar una **tierra (GND) común** entre todos los ESP32 para evitar ruido o errores de trama.

---

## ⚠️ Notas de Implementación
- **Alimentación:** VCC → **3.3V**. Todos los pines operan a nivel lógico de 3.3V.
- **Baudrate:** El número de destellos en el **LED Rojo** indica la velocidad:
  - 1 parpadeo: 9600 bps
  - 5 parpadeos: 115200 bps
- **Reset:** Si cambias la velocidad en la interfaz y el LED no cambia, pulsa el botón EN (Reset) del ESP32 Slave.
