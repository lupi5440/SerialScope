# 📌 Guía de Conexiones — Visualizador SerialScope

Este dispositivo actúa como el **corazón del sistema**, capturando tráfico de forma pasiva en buses I2C/SPI y funcionando como puente (Proxy) en UART. Envía todos los datos a la interfaz web vía **WiFi**.

---

## 🚦 LEDs Indicadores
Conecta cada LED con una resistencia de **220 Ω a 330 Ω** entre el pin y GND.

| Indicador | Pin GPIO | Color LED | Color Cable Sugerido | Comportamiento |
|:---|:---:|:---|:---|:---|
| **Modo UART** | **14** 🔴 | 🔴 Rojo | 🔴 Rojo | Parpadea según el baudrate activo. |
| **Modo I²C** | **27** 🔵 | 🔵 Azul | 🔵 Azul | Fijo si el modo I2C está activo. |
| **Modo SPI** | **13** 🟡 | 🟡 Amarillo | 🟡 Amarillo | Fijo si el modo SPI está activo. |
| **WiFi / Sync** | **2** ⚪ | ⚪ Blanco | ⚪ Blanco | Parpadea buscando WiFi; fijo al conectar y se apaga. |
| **Error / OK** | **15** 🟢 | 🟢 Verde | 🟢 Verde | Parpadea al recibir comandos (PING). |

---

## 🧪 Conexiones de Protocolo

### 📟 UART (Proxy Transparente)
El visualizador intercepta la línea entre el Maestro y el Sensor/Slave.

| Lado Conexión | Función | Pin GPIO | Color Cable Sugerido |
|:---|:---|:---:|:---|
| **Maestro (CH1)** | **RX** (escucha) | **16** 🟣 | 🟣 Morado (Nativo UART2) |
| **Maestro (CH1)** | **TX** (reenvía) | **17** 🟤 | 🟤 Café (Nativo UART2) |
| **Slave (CH2)** | **RX** (escucha) | **33** 🟣 | 🟣 Morado |
| **Slave (CH2)** | **TX** (reenvía) | **32** 🟤 | 🟤 Café |

---

### 🌐 I²C (Sniffing Pasivo)
Captura datos sin interferir en el bus original.

| Función | Pin GPIO | Color Cable Sugerido | Nota Técnica |
|:---|:---:|:---|:---|
| **SCL** (Reloj) | **22** 🟡 | 🟡 Amarillo | Captura por interrupción ultra-rápida. |
| **SDA** (Datos) | **21** 🟢 | 🟢 Verde | Detección de START/STOP por hardware. |

---

### ⚡ SPI (Sniffing Pasivo)
Ideal para interceptar pantallas TFT o sensores de alta velocidad.

| Función | Pin GPIO | Color Cable Sugerido | Nota Técnica |
|:---|:---:|:---|:---|
| **SCK** (Reloj) | **18** 🔘 | 🔘 Gris | Gatilla la captura en nanosegundos. |
| **MISO** | **19** ⚪ | ⚪ Blanco | Captura respuesta del esclavo. |
| **MOSI** | **23** 🔵 | 🔵 Azul | Captura orden del maestro. |
| **CS** | **5** 🟠 | 🟠 Naranja | Habilita/Deshabilita el sniffer. |

---

## ⚠️ Notas de Optimización e Implementación
- **Lectura Rápida:** Los buses I2C y SPI usan **lectura directa de registros (REG_READ)**. Esto permite capturar datos a velocidades que el `digitalRead` normal perdería.
- **Buffers Circulares:** Se implementó un buffer de **512 a 1024 bytes** para evitar pérdida de datos cuando el WiFi está congestionado.
- **Voltaje:** Funciona estrictamente a **3.3V**. El uso de 5V dañará el ESP32 permanentemente.

## 🛠️ Solución de Problemas (Troubleshooting)
- **¿Datos basura?** Revisa que el cable **GND** sea común entre todos los dispositivos.
- **¿No conecta al WiFi?** El LED Blanco parpadeará. Asegúrate de que no haya interferencias de otras redes en 2.4GHz.
- **¿I2C no captura nada?** Verifica que los cables SCL y SDA no estén invertidos.

---

## 📓 Bitácora de Desarrollo e Intentos Fallidos
Esta sección documenta los desafíos técnicos superados durante el desarrollo del Visualizador:

1.  **Hardware SPI Slave (No funcional):** 
    - **Intento:** Originalmente se trató de usar el periférico `SPI Slave` nativo del ESP32 para capturar el tráfico del bus.
    - **Problema:** El hardware SPI Slave del ESP32 es muy rígido con el pin **CS** y los tiempos de respuesta. Al intentar capturar el tráfico de la pantalla **TFT ST7735**, el sniffer perdía la sincronización y bloqueaba el bus, impidiendo que la pantalla mostrara imágenes.
    - **Solución:** Se migró a una captura por **Interrupciones (ISRs)** y lectura directa de registros de entrada/salida (GPIO), lo que permite al ESP32 actuar como un monitor pasivo invisible y ultra-rápido.

2.  **Bluetooth BLE vs WebSockets:**
    - **Intento:** La primera versión del Visualizador usaba **Bluetooth Low Energy (BLE)** para enviar los datos a la web, similar al módulo Maestro/Slave.
    - **Problema:** El protocolo BLE tiene un ancho de banda muy limitado (**MTU de 20-244 bytes**). Al capturar ráfagas de datos de I2C a 400kHz o SPI a alta velocidad, el buffer del Bluetooth se saturaba instantáneamente, causando retrasos de varios segundos o el reinicio del ESP32.
    - **Solución:** Se implementó comunicación vía **WebSockets sobre WiFi**. Esto permite una transmisión masiva de datos en tiempo real (milisegundos) sin saturar el procesador, garantizando que el gráfico en la web sea fluido.
