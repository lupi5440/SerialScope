/*
 * =====================================================================================
 *  PROYECTO : SerialScope — Módulo de Pruebas (Master)
 *  ARCHIVO  : esp32_pruebas_maestro.ino
 * =====================================================================================
 *
 *  DESCRIPCIÓN:
 *    Este es el "Cerebro" del banco de pruebas (Master). Su función es generar
 *    tráfico real y controlado en los buses UART, I2C y SPI para su análisis.
 *
 *  CARACTERÍSTICAS TÉCNICAS:
 *    - BLE UART: Control inalámbrico mediante interfaz Bluetooth.
 *    - UART : Generador de mensajes seriales a baudios variables.
 *    - I2C : Lectura e interpretacion de sensores  (BMP180 / TMP102).
 *    - SPI : Control de pantallas TFT (ST7735) y lectura de termopares tipo k (MAX6675).
 *
 *  CONEXIÓN DE PROTOCOLOS SUGERIDA:
 *    - UART : RX:16, TX:17
 *    - I2C : SDA:21, SCL:22
 *    - SPI : SCK:18, MISO:19, MOSI:23, CS:5 (MAX6675)
 *    - TFT Aux    : A0: 25 , RST:26, LED:27, 
 *    - La TFT tiene en el chip SDA pero en realidad es el MOSI     
*
 *  INDICADORES DE MODO SUGERIDO :
 *    - Verde (Status)  : GPIO 13
 *    - Blanco (BLE)    : GPIO 4
 *    - Rojo (UART)     : GPIO 14
 *    - Amarillo (I2C)  : GPIO 33
 *    - Azul (SPI)      : GPIO 32
 *=====================================================================================*/

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Wire.h>
#include <SPI.h>
#include "max6675.h"
#include <Adafruit_GFX.h>    
#include <Adafruit_ST7735.h> 

// --- BUS SPI ESTÁNDAR (Compartido) ---
#define SPI_SCK   18
#define SPI_MISO  19
#define SPI_MOSI  23
#define SPI_CS 5 

// --- PINES AUXILIARES PANTALLA ---
#define TFT_RST   26  // Reset (Reinicio físico de la pantalla)
#define TFT_DC    25  // Data/Command (Selector de Datos o Comandos)
#define TFT_LED   27  // Backlight (Control de Brillo/Luz de fondo)

// Objetos de Hardware (SPI)
MAX6675 thermocouple(SPI_SCK, SPI_CS, SPI_MISO);
Adafruit_ST7735 tft = Adafruit_ST7735(SPI_CS, TFT_DC, TFT_RST);
String spiProfile = "";

// UUIDs BLE UART Service
#define SERVICE_UUID           "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_RX "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_TX "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// Tiempos y parametros del sistema
#define BLE_BLINK_INTERVAL_MS      500   // Parpadeo cuando busca conexión BLE
#define PING_LED_DURATION_MS       500   // Tiempo del LED verde con PING
#define PROTOCOL_BLINK_INTERVAL_MS 200   // Velocidad de parpadeo de LEDs de protocolo
#define PROTOCOL_BLINK_REST_MS     1500  // Pausa entre secuencias de parpadeo
#define SENSOR_POLL_INTERVAL_MS    1000  // Cada cuánto hace "spam" de lectura a los sensores
#define BLE_CHUNK_SIZE             20    // Tamaño máximo del paquete de envío Bluetooth
#define BLE_MTU_SIZE               512   // Tamaño máximo de negociación Bluetooth
#define BLE_SEND_DELAY_MS          30    // Pausa entre envío de fragmentos BLE
#define UART_MAX_RX_BUFFER         256   // Límite de seguridad para lectura de UART

// Parametros de la pantalla TFT PWM
#define TFT_PWM_FREQ               5000  // Frecuencia PWM para la pantalla
#define TFT_PWM_RES                8     // Resolución PWM (8 bits = 0 a 255)
#define TFT_PWM_MAX                255   // Brillo máximo

// Estados de los modos del master
enum Modo { IDLE, UART_EMU, I2C_EMU, SPI_EMU };
Modo modoActual = IDLE;
unsigned long ultimaTarea = 0;
unsigned long contadorUART = 0;
bool emulacionActiva = false;
String comandoPendiente = ""; // Cola para procesar comandos BLE 

// --- PINES DE CONTROL Y HARDWARE ---
#define LED_STATUS_VERDE  13  // Verde (Alerta)
#define LED_BLE_BLANCO    4   // Blanco (BLE)
#define LED_UART_ROJO     14  // Rojo (UART)
#define LED_I2C_AMARILLO  33  // Amarillo (I2C)
#define LED_SPI_AZUL      32  // Azul (SPI)

#define UART_RX_PIN       16  // Pin recepción UART pruebas
#define UART_TX_PIN       17  // Pin transmisión UART pruebas
#define I2C_SDA_PIN       21  // Pin SDA para bus I2C
#define I2C_SCL_PIN       22  // Pin SCL para bus I2C

// Variables para el parpadeo de los LEDs
int ledBlinkTarget = 0;
int ledBlinkCount = 0;
bool ledBlinkState = false;
unsigned long lastProtocolLedTime = 0;
bool ledResting = false;
unsigned long greenLedTurnOffTime = 0;
unsigned long lastBlinkTime = 0;
bool blinkState = false;

// Parámetros Dinámicos I2C
String i2cProfile = "";
int i2cAddress = 0x77; // Default BMP180

// Prototipos de funciones
void ejecutarComando(String comando);
void bleSend(String texto);
void actualizarLeds(Modo m, int blinks);

// Callbacks BLE UART
class MyServerCallbacks: public BLEServerCallbacks {

    // Callback cuando el dispositivo se conecta
    void onConnect(BLEServer* pServer) { 
        deviceConnected = true; 
        Serial.println("Bluetooth Conectado"); 
    }

    // Callback cuando el dispositivo se desconecta
    void onDisconnect(BLEServer* pServer) { 
        deviceConnected = false; 
        digitalWrite(LED_BLE_BLANCO, LOW); 
        Serial.println("Bluetooth Desconectado"); 
        modoActual = IDLE;
        actualizarLeds(IDLE, 0);
    }
};

// Implementación de Callbacks para obtener comando recibido por ble de la interfaz
class MyCallbacks: public BLECharacteristicCallbacks {

    void onWrite(BLECharacteristic *pCharacteristic) {
        String rxValue = String(pCharacteristic->getValue().c_str());
        if (rxValue.length() > 0) {
            rxValue.trim();
            comandoPendiente = rxValue;
        }
    }
};

// Función para actualizar los LEDs
void actualizarLeds(Modo m, int blinks) {
    digitalWrite(LED_UART_ROJO, LOW);
    digitalWrite(LED_SPI_AZUL, LOW);
    digitalWrite(LED_I2C_AMARILLO, LOW);
    digitalWrite(LED_STATUS_VERDE, LOW); // Limpiar alertas al cambiar de modo
    modoActual = m;
    ledBlinkTarget = blinks;
    ledBlinkCount = 0;
    ledBlinkState = false;
    ledResting = false;
    lastProtocolLedTime = millis();
}

void setup() {

    // Inicio serial
    Serial.begin(115200);
    delay(500); 
    Serial.println("[SISTEMA] >>> ESP32 arrancando...");

    // Pines de los leds
    pinMode(LED_UART_ROJO, OUTPUT);
    pinMode(LED_SPI_AZUL, OUTPUT);
    pinMode(LED_I2C_AMARILLO, OUTPUT);
    pinMode(LED_BLE_BLANCO, OUTPUT);
    pinMode(LED_STATUS_VERDE, OUTPUT);
    
    // Configuración del bus SPI
    pinMode(SPI_CS, OUTPUT);
    digitalWrite(SPI_CS, HIGH);
    pinMode(SPI_MISO, INPUT);
    
    // Configuración del PWM para la pantalla TFT
    ledcAttach(TFT_LED, TFT_PWM_FREQ, TFT_PWM_RES);
    ledcWrite(TFT_LED, TFT_PWM_MAX);

    // Estado inicial de los LEDs
    actualizarLeds(IDLE, 0);
    digitalWrite(LED_BLE_BLANCO, LOW);
    digitalWrite(LED_STATUS_VERDE, LOW);

    // Configuración del bus Bluetooth
    Serial.println("[SISTEMA] >>> Activando bluetooth...");
    BLEDevice::init("SerialScope Master");
    BLEDevice::setMTU(BLE_MTU_SIZE);
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    BLEService *pService = pServer->createService(SERVICE_UUID);
    
    // Característica de transmisión (Escritura desde el Master)
    pTxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
    pTxCharacteristic->addDescriptor(new BLE2902());

    // Característica de recepción (Lectura desde el Esclavo)
    BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
    pRxCharacteristic->setCallbacks(new MyCallbacks());
    
    // Iniciamos el servicio y la publicidad
    pService->start();
    
    // Publicidad: Importante para un handshake rápido y estable
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->start();
    
    Serial.println("[SISTEMA] >>> Maestro listo y visible.");
}

// Función para procesar las tareas recibidas por BLE y hacer solicitured spam al sensor
void processTasks() {
    if (modoActual == SPI_EMU) {
        // MAX6675, hacer solicitured spam cada SENSOR_POLL_INTERVAL_MS tiempo
        if (spiProfile == "MAX6675" && millis() - ultimaTarea > SENSOR_POLL_INTERVAL_MS) {
            float celsius = thermocouple.readCelsius();
            String res;
            if (isnan(celsius)) {
                res = "Error: Sin Sensor";
                digitalWrite(LED_STATUS_VERDE, HIGH);
            } else if (celsius == 0.00) {
                res = "Aviso: Lectura 0.00 (Revisar Cables)";
                digitalWrite(LED_STATUS_VERDE, HIGH);
            } else {
                res = "Temp: " + String(celsius) + " C";
                digitalWrite(LED_STATUS_VERDE, LOW);
            }
            Serial.println("[SPI] >>> " + res);
            bleSend("RESULTADO:" + res); // Eliminado espacio tras RESULTADO: para ahorrar bytes
            ultimaTarea = millis();
        }   
    } else if (modoActual == SPI_EMU && spiProfile == "TFT") {
        // TFT no hacemos spam de lectura, esperamos comandos EMU_MSG
    } else if (modoActual == I2C_EMU) {
        // BMP180, hacer solicitured spam cada SENSOR_POLL_INTERVAL_MS ms
        if (millis() - ultimaTarea > SENSOR_POLL_INTERVAL_MS) {
            String msg = "";
            if (i2cProfile == "BMP180") {
                // Tráfico I2C para BMP180
                Wire.beginTransmission(i2cAddress);
                Wire.write(0xF4);
                Wire.write(0x2E); 
                if (Wire.endTransmission() == 0) {
                    delay(5);
                    Wire.beginTransmission(i2cAddress);
                    Wire.write(0xF6); 
                    Wire.endTransmission();
                    Wire.requestFrom((uint16_t)i2cAddress, (uint8_t)2);
                    if (Wire.available() == 2) {
                        uint8_t msb = Wire.read();
                        uint8_t lsb = Wire.read();
                        int val = (msb << 8) | lsb;
                        msg = "BMP180 (0x" + String(i2cAddress, HEX) + "): " + String(val);
                        digitalWrite(LED_STATUS_VERDE, LOW); 
                    } else {
                        // Si no recibimos datos, indicamos error
                        msg = "Error BMP180 en 0x" + String(i2cAddress, HEX);
                        digitalWrite(LED_STATUS_VERDE, HIGH);
                    }
                } else {
                    // Si no hay ACK, indicamos error
                    msg = "NACK: BMP180 en 0x" + String(i2cAddress, HEX);
                    digitalWrite(LED_STATUS_VERDE, HIGH);
                }
            } else if (i2cProfile == "TMP") {
                // TMP102: Lectura de temperatura cada 1 segundo
                Wire.beginTransmission(i2cAddress);
                Wire.write(0x00);
                if (Wire.endTransmission() == 0) {
                    Wire.requestFrom((uint16_t)i2cAddress, (uint8_t)2);
                    if (Wire.available() == 2) {
                        uint8_t msb = Wire.read();
                        uint8_t lsb = Wire.read();
                        int val = ((msb << 8) | lsb) >> 4;
                        msg = "TMP102 (0x" + String(i2cAddress, HEX) + "): " + String(val);
                        digitalWrite(LED_STATUS_VERDE, LOW); 
                    } else {
                        // Si no recibimos datos, indicamos error
                        msg = "Error TMP102 en 0x" + String(i2cAddress, HEX);
                        digitalWrite(LED_STATUS_VERDE, HIGH);
                    }
                } else {
                    // Si no hay ACK, indicamos error
                    msg = "NACK: TMP102 en 0x" + String(i2cAddress, HEX);
                    digitalWrite(LED_STATUS_VERDE, HIGH);
                }
            }
            Serial.println("[I2C] >>> " + msg);
            bleSend("RESULTADO:" + msg);
            ultimaTarea = millis();
        }
    }
}

// Hilo principal (Loop)
void loop() {

    // 1. PROCESAR COMANDOS PENDIENTES
    if (comandoPendiente != "") {
        String cmd = comandoPendiente;
        comandoPendiente = "";
        ejecutarComando(cmd);
    }

    // 2. GESTIÓN DE CONEXIÓN BLE
    if (!deviceConnected && oldDeviceConnected) { 
        delay(500); //Esperamos 500ms antes de volver a publicitar tras una desconexion
        pServer->startAdvertising(); 
        oldDeviceConnected = deviceConnected; 
    }
    if (deviceConnected && !oldDeviceConnected) { 
        oldDeviceConnected = deviceConnected; 
    }
    
    // Lógica del LED Blanco (Indicador de Conexión BLE)
    if (!deviceConnected) {
        // Parpadea si no hay clientes BLE conectados
        if (millis() - lastBlinkTime > BLE_BLINK_INTERVAL_MS) {
            blinkState = !blinkState;
            digitalWrite(LED_BLE_BLANCO, blinkState ? HIGH : LOW);
            lastBlinkTime = millis();
        }
    } else {
        // Conexión activa: LED siempre encendido
        digitalWrite(LED_BLE_BLANCO, HIGH);
    }

    // Apagar temporalmente el LED verde tras un PONG
    if (greenLedTurnOffTime > 0 && millis() > greenLedTurnOffTime) {
        digitalWrite(LED_STATUS_VERDE, LOW);
        greenLedTurnOffTime = 0;
    }

    // Lógica de parpadeo de LEDs de Protocolos
    if (modoActual != IDLE && ledBlinkTarget > 0) {
        unsigned long currentMillis = millis();
        int activePin = (modoActual == UART_EMU) ? LED_UART_ROJO : (modoActual == I2C_EMU ? LED_I2C_AMARILLO : LED_SPI_AZUL);
        
        // Esperamos a que termine el reposo antes de parpadear
        if (ledResting) {
            if (currentMillis - lastProtocolLedTime > PROTOCOL_BLINK_REST_MS) {
                ledResting = false;
                ledBlinkCount = 0;
                lastProtocolLedTime = currentMillis;
            }
        // Blink del LED de estado (Verde) para indicar actividad
        } else {
            if (currentMillis - lastProtocolLedTime > PROTOCOL_BLINK_INTERVAL_MS) {
                lastProtocolLedTime = currentMillis;
                ledBlinkState = !ledBlinkState;
                digitalWrite(activePin, ledBlinkState ? HIGH : LOW);
                
                if (!ledBlinkState) { // Se acaba de apagar
                    ledBlinkCount++;
                    if (ledBlinkCount >= ledBlinkTarget) {
                        ledResting = true;
                        digitalWrite(activePin, LOW);
                    }
                }
            }
        }
    }

    if (modoActual != IDLE && emulacionActiva) {
        processTasks();
    }

    // ESCUCHA UART (RECIBIR DEL SLAVE) 
    // Si el Slave nos responde por el cable UART, lo notificamos por BLE
    if (modoActual == UART_EMU && Serial2.available()) {
        String uartIncoming = "";
        uartIncoming.reserve(UART_MAX_RX_BUFFER); // Pre-asigna memoria, evita fragmentación

        while(Serial2.available()) {
            uartIncoming += (char)Serial2.read();
            if (uartIncoming.length() >= UART_MAX_RX_BUFFER) break; // Límite de seguridad
        }
        
        Serial.printf("[UART] >>> RX Slave: %s\n", uartIncoming.c_str());
        bleSend(uartIncoming); 
    }

    delay(10);
}

// Función auxiliar para extraer parámetros de un comando (Formato: CMD:P1:P2:P3)
String getParam(String data, int index) {
    int found = 0;
    int strIndex[] = {0, -1};
    int maxIndex = data.length() - 1;

    for (int i = 0; i <= maxIndex && found <= index; i++) {
        if (data.charAt(i) == ':' || i == maxIndex) {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i + 1 : i;
        }
    }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

//Interprete de comandos recibidos por BLE
void ejecutarComando(String cmd) {
    Serial.println("[BLE] >>> " + cmd);
    
    // 1. PING
    if (cmd == "PING") { 
        bleSend("PONG"); 
        digitalWrite(LED_STATUS_VERDE, HIGH);
        greenLedTurnOffTime = millis() + PING_LED_DURATION_MS;
        return; 
    }
    
    // 2. EMU_STOP: Detener emulación
    if (cmd == "EMU_STOP") { 
        emulacionActiva = false;
        digitalWrite(LED_STATUS_VERDE, LOW); // Resetear estado visual al detener
        
        // liberacion del bus spi para que no haya colisiones
        digitalWrite(SPI_CS, HIGH); 
        
        Serial.println("[SISTEMA] >>> Prueba detenida.");
        bleSend("STOP_OK"); 
        return; 
    }

    // 3. EMU_CONFIG: Configurar parámetros de prueba
    if (cmd.startsWith("EMU_CONFIG:")) {
        String proto = getParam(cmd, 1);
        modoActual = IDLE;

    
        if (proto == "UART") {
            long baud = getParam(cmd, 3).toInt();
            String profile = getParam(cmd, 4);
            int blinks = (baud >= 115200) ? 5 : 1;

            Serial2.flush();
            Serial2.end();
            Serial2.begin(baud, SERIAL_8N1, 16, 17);
            actualizarLeds(UART_EMU, blinks);
            contadorUART = 0;
            digitalWrite(LED_STATUS_VERDE, LOW);

            Serial.println("[SISTEMA] >>> Configurando UART | Baud: " + String(baud) + " | Perfil: " + profile);
            bleSend("READY:UART");
            bleSend("RESULTADO: Iniciando envio UART a " + String(baud) + " baud...");

        } else if (proto == "I2C") {
            String addrStr = getParam(cmd, 3);
            long speed = getParam(cmd, 4).toInt();
            i2cProfile = getParam(cmd, 5);
            
            addrStr.replace("0x", "");
            i2cAddress = strtol(addrStr.c_str(), NULL, 16);
            if (i2cAddress == 0) i2cAddress = 0x77; 
            
            int blinks = (speed == 400000) ? 2 : 1;

            Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
            Wire.setClock(speed);
            actualizarLeds(I2C_EMU, blinks);
            digitalWrite(LED_STATUS_VERDE, LOW);

            Serial.println("[SISTEMA] >>> Configurando I2C | Addr: 0x" + String(i2cAddress, HEX) + " | Speed: " + String(speed/1000) + "kHz | Perfil: " + i2cProfile);
            bleSend("READY:I2C");
            bleSend("RESULTADO: Iniciando sondeo (" + i2cProfile + ") en 0x" + String(i2cAddress, HEX) + " a " + String(speed/1000) + " kHz...");

        } else if (proto == "SPI") {
            digitalWrite(SPI_CS, HIGH); // Asegurar bus libre antes de empezar
            int modeIdx = getParam(cmd, 3).toInt();
            spiProfile = getParam(cmd, 4);
            int blinks = modeIdx + 1;

            actualizarLeds(SPI_EMU, blinks);
            
            if (spiProfile == "TFT") {
                Serial.println("[SISTEMA] >>> Inicializando Pantalla TFT...");
                
                // 1. Limpiamos el bus por si veníamos del sensor
                SPI.end(); 
                
                // 2. Iniciamos SPI dejando el CS libre (-1)
                SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, -1);
                
                // 3. Inicializamos la pantalla (¡Aquí Adafruit secuestra el pin 5 internamente!)
                tft.initR(INITR_BLACKTAB); 
                
                // 4. Forzamos el pin 5 a desenlazarse del hardware SPI para que podamos cambiar el cs en caliente
                pinMode(SPI_CS, OUTPUT);
                digitalWrite(SPI_CS, HIGH); 
                
                tft.setRotation(1);
                tft.fillScreen(ST7735_BLACK);
                tft.setTextColor(ST7735_CYAN);
                tft.setTextSize(1);
                tft.setCursor(10, 10);
                tft.println("SerialScope");
                tft.drawFastHLine(0, 22, 160, ST7735_CYAN);
                tft.setCursor(10, 30);
                tft.setTextColor(ST7735_WHITE);
                tft.println("SPI: ST7735 128x160");
                tft.setCursor(10, 50);
                tft.println("Esperando texto...");
                
            } else if (spiProfile == "MAX6675") {
                Serial.println("[SISTEMA] >>> Inicializando Sensor MAX6675...");
                
                // 1. Apagamos el hardware SPI por completo
                SPI.end(); 
                
                // 2. Forzamos todos los pines a modo manual para el bit-banging del sensor
                pinMode(SPI_SCK, OUTPUT);
                pinMode(SPI_MISO, INPUT);
                pinMode(SPI_MOSI, OUTPUT);
                pinMode(SPI_CS, OUTPUT);
                digitalWrite(SPI_CS, HIGH);
            }

            Serial.println("[SISTEMA] >>> Configurando SPI | Modo: " + String(modeIdx) + " | Perfil: " + spiProfile);
            bleSend("READY:SPI");
            String resMsg = (spiProfile == "TFT") ? "Pantalla TFT Lista" : "Leyendo sensor MAX6675";
            bleSend("RESULTADO: " + resMsg + " en SPI Modo " + String(modeIdx) + "...");
        }
        ultimaTarea = millis() - 2000;
        emulacionActiva = false; 
        return;
    } else if (cmd == "EMU_START") {
        emulacionActiva = true;
        Serial.println("[SISTEMA] >>> Reanudando prueba...");
        bleSend("EMU_OK");
    } else if (cmd.startsWith("EMU_MSG:")) {
        int f = cmd.indexOf(':');
        int s = cmd.indexOf(':', f+1);
        String p = cmd.substring(f+1, s);
        String data = cmd.substring(s+1);
        if (p == "UART") {
            Serial2.print(data);
            digitalWrite(LED_STATUS_VERDE, HIGH);
            greenLedTurnOffTime = millis() + PROTOCOL_BLINK_INTERVAL_MS;
        } else if (p == "I2C") {

            // Caso 1: Solicitud de calibración BMP180
            if (data == "READ_CALIB") {
                Serial.print("\n[SISTEMA] >>> Solicitando tabla de calibración al sensor en 0x");
                Serial.println(i2cAddress, HEX);
                
                Wire.beginTransmission(i2cAddress);
                Wire.write(0xAA);
                
                if (Wire.endTransmission() != 0) {
                    Serial.print("[ERROR] No se detecta el sensor en 0x");
                    Serial.println(i2cAddress, HEX);
                    bleSend("RESULTADO:Error: Sensor NACK");
                    return;
                }

                Wire.requestFrom(i2cAddress, 22);
                String hexBuffer = "";
                hexBuffer.reserve(70); // Protege la RAM al construir la cadena larga (22 bytes * 3 chars/byte + comas)

                if (Wire.available() == 22) {
                    for (int i = 0; i < 22; i++) {
                        uint8_t b = Wire.read();
                        if (b < 0x10) hexBuffer += "0";
                        hexBuffer += String(b, HEX);
                        if (i < 21) hexBuffer += ",";
                    }
                    Serial.println("[OK] Calibración obtenida: " + hexBuffer);
                    bleSend("RESULTADO:" + hexBuffer);
                }
            } 

            // Caso 2: Escritura manual de registro (ej: ADDR:REG DATA)
            else if (data.indexOf(':') != -1) {
                int sep = data.indexOf(':');
                String addrStr = data.substring(0, sep);
                String payload = data.substring(sep + 1);
                
                int addr = strtol(addrStr.c_str(), NULL, 16);
                
                Serial.print("[I2C WRITE] Destino: 0x"); Serial.print(addr, HEX);
                Serial.print(" | Datos: "); Serial.println(payload);
                
                Wire.beginTransmission(addr);
                // Parsear bytes separados por espacio
                int startIndex = 0;
                for (int i = 0; i <= payload.length(); i++) {
                    if (payload.charAt(i) == ' ' || i == payload.length()) {
                        String bStr = payload.substring(startIndex, i);
                        Wire.write((uint8_t)strtol(bStr.c_str(), NULL, 16));
                        startIndex = i + 1;
                    }
                }
                uint8_t error = Wire.endTransmission();
                if (error == 0) {
                    bleSend("I2C_TX_OK");
                    digitalWrite(LED_STATUS_VERDE, HIGH);
                    greenLedTurnOffTime = millis() + PING_LED_DURATION_MS;
                } else {
                    bleSend("I2C_TX_ERR:" + String(error));
                }
            }
        } else if (p == "SPI_TFT") {
            Serial.print("[SPI TFT] Texto: "); Serial.println(data);
            tft.fillRect(0, 70, 160, 40, ST7735_BLACK);
            tft.setCursor(10, 80);
            tft.setTextColor(ST7735_GREEN);
            tft.setTextSize(2);
            tft.println(data);
            bleSend("TFT_OK");
        } else if (p == "SPI_TFT_BRI") {
            int val = data.toInt();
            Serial.print("[SPI TFT] Brillo: "); Serial.println(val);
            ledcWrite(TFT_LED, val);
        }
    }
}

// Envía mensajes largos dividiéndolos en fragmentos compatibles con BLE (Max 20 caracteres)
void bleSend(String texto) {
    if (deviceConnected) {
        String payload = texto + "\n";
        int totalLen = payload.length();
        int offset = 0;
        
        while (offset < totalLen) {
            int currentChunkSize = min(BLE_CHUNK_SIZE, totalLen - offset);
            pTxCharacteristic->setValue((uint8_t*)(payload.c_str() + offset), currentChunkSize);
            pTxCharacteristic->notify();
            offset += currentChunkSize;
            delay(BLE_SEND_DELAY_MS);
        }
    }
}
