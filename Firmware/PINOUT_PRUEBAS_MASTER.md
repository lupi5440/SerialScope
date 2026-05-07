# 📌 Guía de Conexiones — ESP32 Master (Pruebas)

Este dispositivo es el **cerebro del banco de pruebas**. Genera tráfico real en UART, I2C y SPI para validar el funcionamiento del sistema SerialScope. Se controla vía **Bluetooth (BLE)**.

---

## 🚦 LEDs Indicadores
Conecta cada LED con una resistencia de **220 Ω a 330 Ω**.

| Indicador | Pin GPIO | Color LED | Color Cable Sugerido | Comportamiento |
|:---|:---:|:---|:---|:---|
| **Modo UART** | **25** 🔴 | 🔴 Rojo | 🔴 Rojo | Parpadea según el baudrate (1=9600...5=115200). |
| **Modo I²C** | **26** 🔵 | 🔵 Azul | 🔵 Azul | Parpadea según frecuencia (1=100k, 2=400k). |
| **Modo SPI** | **27** 🟡 | 🟡 Amarillo | 🟡 Amarillo | Parpadea según el modo SPI (1 a 4). |
| **Estado BLE** | **32** ⚪ | ⚪ Blanco | ⚪ Blanco | Parpadea buscando conexión; fijo al conectar. |
| **Status OK** | **33** 🟢 | 🟢 Verde | 🟢 Verde | Parpadea en éxitos (PONG) o errores de sensor. |

---

## 🧪 Conexiones de Protocolo

### 📟 UART
| Función ESP32 | Pin GPIO | Color Cable Sugerido |
|:---|:---:|:---|
| **RX** (Escucha) | **16** 🟣 | 🟣 Morado |
| **TX** (Envía) | **17** 🟤 | 🟤 Café |

---

### 🌐 I²C (Sensores BMP180 / TMP102)
| Función | Pin GPIO | Color Cable Sugerido | Dirección I2C |
|:---|:---:|:---|:---|
| **SCL** (Reloj) | **22** 🟡 | 🟡 Amarillo | 0x77 (BMP180) |
| **SDA** (Datos) | **21** 🟢 | 🟢 Verde | 0x48 (TMP102) |

---

### ⚡ SPI (MAX6675 o TFT ST7735)

| Señal SPI | Pin GPIO | Color Cable Sugerido | Función |
|:---|:---:|:---|:---|
| **SCK** | **18** 🔘 | 🔘 Gris | Reloj |
| **CS** | **5** 🟠 | 🟠 Naranja | Chip Select |
| **MISO** | **19** ⚪ | ⚪ Blanco | Solo para MAX6675 (DO) |
| **MOSI** | **23** 🔵 | 🔵 Azul | Solo para TFT (SDA) |
| **DC** | **2** 🟣 | 🟣 Morado | Solo TFT (Data/Command) |
| **RST** | **4** 🔵 | 🔵 Azul Claro | Solo TFT (Reset) |
| **LED** | **13** 🟢 | 🟢 Verde Lima | Control Brillo TFT (PWM) |

---

## ⚠️ Notas de Implementación (Master)
- **Brillo PWM:** Se usa la nueva API `ledcAttach` (Core 3.0) para un control de brillo suave en la pantalla.
- **Estabilización:** El sensor MAX6675 requiere 500ms de calma tras el encendido para dar lecturas reales.

## 🛠️ Solución de Problemas (Troubleshooting)
- **¿Lectura de 0.00?** El sensor no está haciendo buen contacto o falta alimentación.
- **¿Pantalla en blanco?** Verifica el pin LED (13). Si no tiene voltaje, la retroiluminación no encenderá.

