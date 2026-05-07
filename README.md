# 🔬 SerialScope — Módulo de Pruebas de Protocolos

Sistema embebido de validación de buses serie (UART, I²C y SPI) controlado de forma inalámbrica vía **Bluetooth Low Energy (BLE)** desde una interfaz web moderna construida con **Vite + Bootstrap 5**.

---

## 🏗️ Arquitectura del Sistema

```
┌─────────────────────────────┐        BLE         ┌──────────────────────────┐
│   Interfaz Web (Vite)       │ ◄─────────────────► │  ESP32 Master (Pruebas)  │
│   pruebas.html + pruebas.js │                     │  esp32_pruebas_maestro   │
└─────────────────────────────┘                     └──────────┬───────────────┘
                                                               │ UART (Serial2)
                                                    ┌──────────▼───────────────┐
                                                    │  ESP32 Slave (Destino)   │
                                                    │  esp32_pruebas_slave     │
                                                    └──────────────────────────┘
```

### Componentes
| Componente | Ruta | Descripción |
|---|---|---|
| **Interfaz Web** | `WebInterface/` | SPA Vite con Bootstrap 5. Controla todo vía BLE |
| **Firmware Master** | `Firmware/esp32_pruebas_maestro/` | ESP32 principal. Ejecuta pruebas UART, I²C y SPI (MAX6675 y TFT) |
| **Firmware Slave** | `Firmware/esp32_pruebas_slave/` | ESP32 secundario. Recibe y reenvía mensajes UART para validación cruzada |

---

## 🚀 Inicio Rápido

### 1. Interfaz Web
```bash
cd WebInterface
npm install
npm run dev
```
Abrir: `http://localhost:5173/pages/pruebas.html`

### 2. Firmware
- Abrir `esp32_pruebas_maestro.ino` en Arduino IDE
- Instalar librerías: `Adafruit GFX`, `Adafruit ST7735`, `MAX6675` (ver sección de librerías)
- Seleccionar placa: **ESP32 Dev Module**
- Subir el sketch

---

## 📡 Protocolo de Comunicación BLE

Todos los mensajes viajan como texto plano sobre el servicio **BLE UART (NUS)**:

| Comando | Dirección | Descripción |
|---|---|---|
| `PING` | Web → ESP32 | Latencia / test de conexión |
| `EMU_CONFIG:PROTO:AUTO:PARAMS` | Web → ESP32 | Configura el protocolo activo |
| `EMU_START` | Web → ESP32 | Inicia el ciclo de lectura automática |
| `EMU_STOP` | Web → ESP32 | Detiene la emulación |
| `EMU_MSG:SPI_TFT:<texto>` | Web → ESP32 | Muestra texto en la pantalla TFT |
| `EMU_MSG:SPI_TFT_BRI:<0-255>` | Web → ESP32 | Ajusta brillo de retroiluminación |
| `EMU_MSG:UART:<datos>` | Web → ESP32 | Envía datos por UART al Slave |
| `EMU_OK` / `TFT_OK` | ESP32 → Web | Confirmación de operación exitosa |
| `RESULTADO:<valor>` | ESP32 → Web | Dato leído del sensor activo |

### Formato EMU_CONFIG
```
EMU_CONFIG:UART:AUTO:<baudrate>:<perfil>
EMU_CONFIG:I2C:AUTO:<direccion_hex>:<velocidad_hz>:<perfil>
EMU_CONFIG:SPI:AUTO:<modo_0-3>:<perfil>
```

---

## 🧪 Protocolos Soportados

### UART
- Módulo MAX3232 para conversión de niveles RS-232
- Baudrates: 9600, 19200, 38400, 57600, 115200
- Comunicación bidireccional Master ↔ Slave

### I²C
- **BMP180** — Sensor barométrico (dirección 0x77)
- **TMP102** — Sensor de temperatura (dirección 0x48 / 0x49)
- Velocidades: 100 kHz (estándar) y 400 kHz (rápido)

### SPI
- **MAX6675** — Termopar Tipo K (solo lectura, ~1 muestra/seg)
- **ST7735** — Pantalla TFT 1.8" 128×160 px con control de brillo PWM

---

## 📚 Librerías Requeridas (Arduino IDE)

Instalar desde **Herramientas → Administrar Librerías**:

| Librería | Versión recomendada |
|---|---|
| `Adafruit GFX Library` | ≥ 1.11 |
| `Adafruit ST7735 and ST7789 Library` | ≥ 1.10 |
| `MAX6675 library` | ≥ 1.1 |

El soporte BLE viene incluido con el **ESP32 Arduino Core ≥ 3.0**.

---

## 📌 Documentación de Pines

| Documento | Descripción |
|---|---|
| [`PINOUT_PRUEBAS_MASTER.md`](./PINOUT_PRUEBAS_MASTER.md) | Pines del ESP32 Master (LEDs, UART, I²C, SPI, TFT) |
| [`PINOUT_PRUEBAS_SLAVE.md`](./PINOUT_PRUEBAS_SLAVE.md) | Pines del ESP32 Slave (LEDs, UART) |
| [`PINOUT_VISUALIZADOR.md`](./PINOUT_VISUALIZADOR.md) | Pines del ESP32 Visualizador |

---

## ⚠️ Notas Importantes

- El bus SPI es **compartido** entre el MAX6675 y la pantalla TFT. **No conectar ambos simultáneamente**. Desconectar uno antes de conectar el otro.
- El control de brillo TFT usa la API `ledcAttach()` del **ESP32 Core 3.0+**. No es compatible con versiones anteriores.
- Todos los pines operan a **3.3 V**. No conectar directamente a 5 V.
