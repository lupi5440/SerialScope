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
 *    - UART Gen: Generador de mensajes seriales a baudios variables.
 *    - I2C Node: Simulación de sensores industriales (BMP180 / TMP102).
 *    - SPI Gen: Control de pantallas TFT y lectura de termopares (MAX6675).
 *
 *  CONEXIÓN Protocolos:
 *    - UART Gen   : RX:16, TX:17
 *    - I2C Node   : SDA:21, SCL:22
 *    - SPI Sensor : SCK:18, MISO:19, MOSI:23, CS:2 (MAX6675)
 *    - TFT Aux    : RST:26, DC:25, LED:27, CS:5
 *
 *  INDICADORES DE MODO :
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
#define SPI_CS    5  

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

// ****************************************************************************************
//                                    LEDs DE CONTROL          
// ****************************************************************************************
#define LED_STATUS_VERDE  13  // Verde (Alerta)
#define LED_BLE_BLANCO    4   // Blanco (BLE)
#define LED_UART_ROJO     14  // Rojo (UART)
#define LED_I2C_AMARILLO  33  // Amarillo (I2C)
#define LED_SPI_AZUL      32  // Azul (SPI)

// Estados de los modos del master
enum Modo { IDLE, UART_EMU, I2C_EMU, SPI_EMU };
Modo modoActual = IDLE;
unsigned long ultimaTarea = 0;
unsigned long contadorUART = 0;
bool emulacionActiva = false;
String comandoPendiente = ""; // Cola para procesar comandos BLE 

// Parámetros Dinámicos I2C
String i2cProfile = "";
int i2cAddress = 0x77; // Default BMP180

// Objetos de Hardware
// (Nota: thermocouple y tft ya están definidos arriba en la sección de pines)

// Variables para el parpadeo de los LEDs
int ledBlinkTarget = 0;
int ledBlinkCount = 0;
bool ledBlinkState = false;
unsigned long lastProtocolLedTime = 0;
bool ledResting = false;
unsigned long greenLedTurnOffTime = 0;
unsigned long lastBlinkTime = 0;
bool blinkState = false;

void ejecutarComando(String comando);
void bleSend(String texto);
void actualizarLeds(Modo m, int blinks);

// ****************************************************************************************
//                                  CALLBACKS BLE UART          
// ****************************************************************************************

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
        String rxValue = pCharacteristic->getValue().c_str();
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
    digitalWrite(LED_STATUS_VERDE, LOW); // Siempre limpiar alertas al cambiar de modo
    modoActual = m;
    ledBlinkTarget = blinks;
    ledBlinkCount = 0;
    ledBlinkState = false;
    ledResting = false;
    lastProtocolLedTime = millis();
}

// ****************************************************************************************
//                                    SETUP PRINCIPAL          
// ****************************************************************************************
void setup() {
    // 1. INICIO SERIAL Y DIAGNÓSTICO (Para ver qué pasa desde el segundo 1)
    Serial.begin(115200);
    delay(500); 
    Serial.println("\n\n========================================");
    Serial.println("[SISTEMA] >>> ESP32 Arrancando...");
    Serial.println("========================================\n");

    // 2. CONFIGURACIÓN DE HARDWARE (Pines)
    pinMode(LED_UART_ROJO, OUTPUT);
    pinMode(LED_SPI_AZUL, OUTPUT);
    pinMode(LED_I2C_AMARILLO, OUTPUT);
    pinMode(LED_BLE_BLANCO, OUTPUT);
    pinMode(LED_STATUS_VERDE, OUTPUT);
    
    pinMode(SPI_CS, OUTPUT);
    digitalWrite(SPI_CS, HIGH);
    pinMode(SPI_MISO, INPUT);
    
    ledcAttach(TFT_LED, 5000, 8);
    ledcWrite(TFT_LED, 255);

    actualizarLeds(IDLE, 0);
    digitalWrite(LED_BLE_BLANCO, LOW);
    digitalWrite(LED_STATUS_VERDE, LOW);

    // 3. INICIALIZACIÓN BLUETOOTH (Al final para mayor estabilidad)
    Serial.println("[SISTEMA] >>> Activando Bluetooth...");
    BLEDevice::init("SerialScope Master");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    BLEService *pService = pServer->createService(SERVICE_UUID);
    
    pTxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
    pTxCharacteristic->addDescriptor(new BLE2902());
    BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
    pRxCharacteristic->setCallbacks(new MyCallbacks());
    
    pService->start();
    
    // PUBLICIDAD: Importante para un handshake rápido y estable
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->start();
    
    Serial.println("[SISTEMA] >>> Maestro listo y visible.");
}

// Función para procesar las tareas recibidas por BLE y hacer solicitured spam al sensor
void processTasks() {
    if (modoActual == SPI_EMU) {
        // MAX6675, hacer solicitured spam cada 1 segundo
        if (spiProfile == "MAX6675" && millis() - ultimaTarea > 1000) {
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
        // BMP180, hacer solicitured spam cada 1 segundo
        if (millis() - ultimaTarea > 1000) {
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
                        msg = "Error BMP180 en 0x" + String(i2cAddress, HEX);
                        digitalWrite(LED_STATUS_VERDE, HIGH);
                    }
                } else {
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
                        msg = "Error TMP102 en 0x" + String(i2cAddress, HEX);
                        digitalWrite(LED_STATUS_VERDE, HIGH);
                    }
                } else {
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

// ****************************************************************************************
//                                    MAIN LOOP          
// ****************************************************************************************
void loop() {
    // 1. PROCESAR COMANDOS PENDIENTES
    if (comandoPendiente != "") {
        String cmd = comandoPendiente;
        comandoPendiente = "";
        ejecutarComando(cmd);
    }

    // 2. GESTIÓN DE CONEXIÓN BLE
    if (!deviceConnected && oldDeviceConnected) { 
        delay(500); 
        pServer->startAdvertising(); 
        oldDeviceConnected = deviceConnected; 
    }
    if (deviceConnected && !oldDeviceConnected) { 
        oldDeviceConnected = deviceConnected; 
    }
    
    // Lógica del LED Blanco (Indicador de Conexión BLE)
    if (!deviceConnected) {
        // Parpadea si no hay clientes BLE conectados
        if (millis() - lastBlinkTime > 500) {
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
        
        if (ledResting) {
            if (currentMillis - lastProtocolLedTime > 1500) {
                ledResting = false;
                ledBlinkCount = 0;
                lastProtocolLedTime = currentMillis;
            }
        } else {
            if (currentMillis - lastProtocolLedTime > 200) {
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

    // --- ESCUCHA UART (RECIBIR DEL SLAVE) ---
    // Si el Slave nos responde por el cable UART, lo notificamos por BLE
    if (modoActual == UART_EMU && Serial2.available()) {
        String uartIncoming = "";
        while(Serial2.available()) {
            uartIncoming += (char)Serial2.read();
        }
        Serial.println("[UART] >>> RX Slave: " + uartIncoming);
        bleSend(uartIncoming); 
    }

    delay(10);
}

// Función auxiliar para extraer parámetros de un comando (Formato: CMD:P1:P2:P3)
// ****************************************************************************************
//                                  FUNCIONES DE UTILIDAD          
// ****************************************************************************************

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

// ****************************************************************************************
//                                    INTERPRETE COMANDOS          
// ****************************************************************************************
void ejecutarComando(String cmd) {
    Serial.println("[BLE] >>> " + cmd);
    
    // 1. PING
    if (cmd == "PING") { 
        bleSend("PONG"); 
        digitalWrite(LED_STATUS_VERDE, HIGH);
        greenLedTurnOffTime = millis() + 500;
        return; 
    }
    
    // 2. EMU_STOP: Detener emulación
    if (cmd == "EMU_STOP") { 
        emulacionActiva = false;
        digitalWrite(LED_STATUS_VERDE, LOW); // Resetear estado visual al detener
        Serial.println("[SISTEMA] >>> Prueba detenida.");
        bleSend("STOP_OK"); 
        return; 
    }

    // 4. I2C_SCAN: Eliminado tras pruebas de diagnóstico


    // 3. EMU_CONFIG: Configurar parámetros de prueba
    if (cmd.startsWith("EMU_CONFIG:")) {
        String proto = getParam(cmd, 1);
        modoActual = IDLE;

        if (proto == "UART") {
            long baud = getParam(cmd, 3).toInt();
            String profile = getParam(cmd, 4);
            int blinks = (baud >= 115200) ? 5 : 1;

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

            Wire.begin(21, 22); 
            Wire.setClock(speed);
            actualizarLeds(I2C_EMU, blinks);
            digitalWrite(LED_STATUS_VERDE, LOW);

            Serial.println("[SISTEMA] >>> Configurando I2C | Addr: 0x" + String(i2cAddress, HEX) + " | Speed: " + String(speed/1000) + "kHz | Perfil: " + i2cProfile);
            bleSend("READY:I2C");
            bleSend("RESULTADO: Iniciando sondeo (" + i2cProfile + ") en 0x" + String(i2cAddress, HEX) + " a " + String(speed/1000) + " kHz...");

        } else if (proto == "SPI") {
            int modeIdx = getParam(cmd, 3).toInt();
            spiProfile = getParam(cmd, 4);
            int blinks = modeIdx + 1;

            actualizarLeds(SPI_EMU, blinks);
            
            if (spiProfile == "TFT") {
                Serial.println("[SISTEMA] >>> Inicializando Pantalla TFT...");
                tft.initR(INITR_BLACKTAB); 
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
            // bleSend(data); // Eliminamos el eco para evitar duplicados en la web
            digitalWrite(LED_STATUS_VERDE, HIGH);
            greenLedTurnOffTime = millis() + 200;
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
                    greenLedTurnOffTime = millis() + 500;
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

// ****************************************************************************************
//                                    COMUNICACIÓN BLE          
// ****************************************************************************************

// Envía mensajes largos dividiéndolos en fragmentos compatibles con BLE (Max 20 caracteres)
void bleSend(String texto) {
    if (deviceConnected) {
        String payload = texto + "\n";
        int totalLen = payload.length();
        int offset = 0;
        const int CHUNK_SIZE = 20; // Tamaño estándar para evitar truncamiento
        
        while (offset < totalLen) {
            int currentChunkSize = min(CHUNK_SIZE, totalLen - offset);
            pTxCharacteristic->setValue((uint8_t*)(payload.c_str() + offset), currentChunkSize);
            pTxCharacteristic->notify();
            offset += currentChunkSize;
            delay(30); // Aumentado para mayor estabilidad en dispositivos lentos
        }
    }
}
