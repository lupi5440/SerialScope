# 📘 Auxiliar de Protocolos: I2C y SPI (Modos Activos y Esclavos)

Este documento detalla el procedimiento para interactuar con dispositivos en modo **Maestro (Activo)** y **Esclavo (Emulado)** usando el ecosistema SerialScope.

---

## 🌐 Protocolo I2C

### 1. Modo Maestro (Visualizador)

Se usa para interrogar a sensores reales o emulados.

#### TMP102 lectura

**Lectura de Temperatura (TMP102):**
- **Dirección:** `0x48` (ADD0 -> GND) o `0x49` (ADD0 -> VCC).
- **Registro:** `0x00` (Temperature Register).
- **Bytes a leer:** `2`.
- **Fórmula de Conversión:**
  1. Unir los dos bytes: `(Byte1 << 8 | Byte2)`.
  2. Desplazar 4 bits a la derecha: `Valor >> 4`.
  3. Multiplicar por la resolución: `Resultado * 0.0625 = °C`.
  *Ejemplo:* `17, D0` -> `0x17D0` -> `0x17D` (381) -> **23.81 °C**.

#### BMP180 lectura

**Lectura de Temperatura (BMP180):**
Este sensor requiere un proceso de **dos pasos** (Escritura + Lectura):

1. **PASO 1 (Pedir lectura)**: 
   - *Dirección:* `0x77` | *Registro:* `0xF4` | *Datos:* `2E`
   - Pulsa **ESCRIBIR**. Esto le dice al sensor que empiece a medir la temperatura.
2. **PASO 2 (Recoger dato)**:
   - Espera un momento (5-10ms).
   - *Dirección:* `0x77` | *Registro:* `0xF6` | *Bytes:* `2`
   - Pulsa **SOLICITAR**. Recibirás el dato crudo (UT), por ejemplo: `6E, 5A`.

**Fórmula de Conversión (Bosch):**
Una vez tienes el dato crudo (`UT`), la temperatura real (`T`) se calcula así:
1. `X1 = (UT - AC6) * AC5 / 2^15`
2. `X2 = (MC * 2^11) / (X1 + MD)`
3. `B5 = X1 + X2`
4. `T = (B5 + 8) / 2^4`
*(El resultado final se divide entre 10 para tener grados Celsius).*

**Lectura de Calibración (BMP180):**
- **Dirección:** `0x77` | **Registro:** `0xAA` | **Bytes:** `22`
- El sensor devuelve 11 valores (2 bytes cada uno) en este orden exacto:
  `AC1, AC2, AC3, AC4, AC5, AC6, B1, B2, MB, MC, MD`
- *Ejemplo:* Los primeros 2 bytes son `AC1`, los siguientes 2 son `AC2`, y así hasta llegar a los últimos 2 que son `MD`.

#### TMP102 escritura (Configuración alarma)

**Configuración de Alarma (TMP102):**
- **Registro:** `0x03` (T-High Register).
- **Bytes a escribir:** 2 bytes en Hex.
- **Lógica de cálculo:** `(Temperatura / 0.0625) << 4`.

| Temperatura deseada | Cálculo (Grados / 0.0625) | Valor Hex (12 bits) | Formato 16 bits (<< 4) | **Datos a poner en Web** |
|:---:|:---:|:---:|:---:|:---|
| **40 °C** | 640 | `0x280` | `0x2800` | **28, 00** |
| **20 °C** | 320 | `0x140` | `0x1400` | **14, 00** |
| **25.5 °C** | 408 | `0x198` | `0x1980` | **19, 80** |

**Pasos en la Interfaz del Visualizador:**
1. Dirección I2C: `0x48`.
2. Registro: `0x03`.
3. Datos (Hex): `28, 00` (para 40°C) o `14, 00` (para 20°C).
4. Botón: **ESCRIBIR**.

### 2. Modo Esclavo (Visualizador)
Se usa para que el Visualizador finja ser un sensor y responda al Maestro (ESP32 Pruebas).

1. **Conexión:** Une SDA(21), SCL(22) y GND de ambas placas.
2. **Visualizador (Esclavo):**
   - Elige `I2C Esclavo`.
   - Dirección: `0x48` | Registro: `0x00`.
   - Datos: `17, D0` (equivale a 23.94°C) o `28, 00` (equivale a 40.98°C)
   - Dale a **Aplicar al Hardware**.
3. **Maestro (Pruebas):**
   - Elige `I2C Maestro`.
   - Perfil: `TMP102` | Dirección: `0x48`.
   - Dale a **Sincronizar Maestro**.
4. **Resultado:** Deberías ver en la pantalla de Pruebas que el Maestro lee exactamente **23.94°C** o **40.98°C**. Si cambias los datos en el Visualizador, la temperatura cambiará en el Maestro.

---

## ⚡ Protocolo SPI

### 1. Modo Maestro (Visualizador)

**IMPORTANTE**: Se debe desconectar del paralelo el maestro del sensor , si se mantiene conectado el Visualizador se puede ver afectado y no funcionara correctamente.

**Operación:** El Visualizador genera el reloj (SCK) y selecciona al esclavo (CS).
**Transferencia:** Envías bytes por MOSI y recibes simultáneamente por MISO.

#### Configuración MAX6675

**Configuración**:
   - Selecciona **SPI (Maestro Activo)**.
   - **Modo**: Selecciona el modo del sensor (Modo 0 es el estándar para MAX6675).
   - **Frecuencia**: Selecciona la velocidad (ej: 1 MHz).
   - Haz clic en **Aplicar al Hardware**. El LED Azul del ESP32 parpadeará indicando la velocidad.
**Transferencia de Datos**:
   - En el cuadro "Datos a transferir", escribe los bytes en HEX.
   - **Para leer el Termopar**: Escribe `00 00`. 
     - *¿Por qué?* Porque el sensor entrega **16 bits** de datos. Cada `00` genera 8 pulsos de reloj. 2 bytes = 16 bits exactos.
     - *¿Puedo pedir más?* No. El sensor solo tiene 16 bits de memoria por lectura. Si pides más (ej: `00 00 00`), recibirás ceros después del segundo byte.
   - Haz clic en **Transferir**.

**Resultado y Conversión (MISO)**:
Al transferir, recibirás algo como `02, B0`.

**¿Cómo convertir `02 B0` a Grados Celsius?**
1. **Unir los bytes**: `0x02` y `0xB0` forman `0x02B0`.
2. **Eliminar bits de estado**: El sensor usa los últimos 3 bits para diagnóstico. Debes desplazar el valor 3 bits a la derecha:
   - En calculadora: `0x02B0 >> 3 = 86` (decimal).
3. **Multiplicar por resolución**: Cada unidad equivale a 0.25°C.
   - `86 * 0.25 = 21.5 °C`.

### 2. Modo Esclavo (Visualizador) - Emulación de Sensor

Este modo sirve para que el Visualizador finja ser un sensor y responda a otor maestro SPI.

#### Caso: Emular Termopar MAX6675

**Configuración en Visualizador (Esclavo)**:
   - Selecciona **SPI (Esclavo)**.
   - **Datos de Respuesta (MISO)**: Escribe los bytes que quieres que el Maestro reciba.
     - *Ejemplo*: Escribe `02 B0` (simula 43 °C) o `02 38` (simula 35.50 °C).
   - Haz clic en **Aplicar al Hardware**. El Visualizador ahora está "en espera".


