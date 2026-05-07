/*
 * =====================================================================================
 *  PROYECTO : SerialScope — Módulo de Pruebas (Master)
 *  ARCHIVO  : esp32_pruebas_maestro.ino
 *  AUTOR    : Juan Angel Serrano Carreñó
 * =====================================================================================
 *
 *  DESCRIPCIÓN:
 *    Este es el "Cerebro" del banco de pruebas (Master).
 *    Genera tráfico real en UART, I2C y SPI para que el Visualizador lo capture.
 *
 *  CARACTERÍSTICAS:
 *    - UART: Envía mensajes al Slave a diferentes baudios.
 *    - I2C: Escucha y escritura para sensores (BMP180, TMP102).
 *    - SPI: Controla una pantalla TFT o lee un termopar MAX6675.
 *
 * =====================================================================================
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Wire.h>
#include <SPI.h>
#include "max6675.h"
#include <Adafruit_GFX.h>    
#include <Adafruit_ST7735.h> 

// Pines TFT (ST7735 128x160)
#define TFT_CS    5   
#define TFT_RST   4 
#define TFT_DC    2
#define TFT_MOSI  23  // SDA en la pantalla
#define TFT_SCLK  18  // SCK en la pantalla
#define TFT_LED   13  // Opcional, control de retroiluminación

// Usar Hardware SPI (más rápido y estable)
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
String spiProfile = "";

// UUIDs BLE UART Service
#define SERVICE_UUID           "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_RX "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_TX "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// LEDs de control
#define LED_MODO_ROJO 25
#define LED_MODO_AZUL 26
#define LED_MODO_AMARILLO 27
#define LED_BLE_BLANCO 32
#define LED_ERROR_VERDE 33

// Estados de los modos del master
enum Modo { IDLE, UART_EMU, I2C_EMU, SPI_EMU };
Modo modoActual = IDLE;
unsigned long ultimaTarea = 0;
unsigned long contadorUART = 0;
bool emulacionActiva = false;

// Parámetros Dinámicos I2C
String i2cProfile = "";
int i2cAddress = 0x77; // Default BMP180

// Pines SPI MAX6675
int thermoDO = 19;
int thermoCS = 5;
int thermoCLK = 18;
MAX6675 thermocouple(thermoCLK, thermoCS, thermoDO);

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

// Implementación de Callbacks para BLE
class MyServerCallbacks: public BLEServerCallbacks {

    // Callback cuando el dispositivo se conecta
    void onConnect(BLEServer* pServer) { 
        deviceConnected = true; 
        digitalWrite(LED_BLE_BLANCO, HIGH); 
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
        String rxValue = pCharacteristic->getValue();
        if (rxValue.length() > 0) {
            rxValue.trim();
            ejecutarComando(rxValue);
        }
    }
};

// Función para actualizar los LEDs
void actualizarLeds(Modo m, int blinks) {
    digitalWrite(LED_MODO_ROJO, LOW);
    digitalWrite(LED_MODO_AZUL, LOW);
    digitalWrite(LED_MODO_AMARILLO, LOW);
    modoActual = m;
    ledBlinkTarget = blinks;
    ledBlinkCount = 0;
    ledBlinkState = false;
    ledResting = false;
    lastProtocolLedTime = millis();
}

void setup() {
    // Inicializa el puerto serial
    Serial.begin(115200);

    // Inicializa los pines de los LEDs
    pinMode(LED_MODO_ROJO, OUTPUT);
    pinMode(LED_MODO_AZUL, OUTPUT);
    pinMode(LED_MODO_AMARILLO, OUTPUT);
    pinMode(LED_BLE_BLANCO, OUTPUT);
    pinMode(LED_ERROR_VERDE, OUTPUT);
    
    // Inicialización explícita de pines SPI para MAX6675
    pinMode(thermoCS, OUTPUT);
    digitalWrite(thermoCS, HIGH); // Desactivar chip inicialmente
    pinMode(thermoDO, INPUT);
    
    // Configuración PWM para Brillo TFT (Nueva API Core 3.0+)
    ledcAttach(TFT_LED, 5000, 8); // Pin, Frecuencia 5kHz, Resolución 8 bits
    ledcWrite(TFT_LED, 255);      // Brillo máximo inicial

    // Inicializa los LEDs de control
    actualizarLeds(IDLE, 0);
    digitalWrite(LED_BLE_BLANCO, LOW);
    digitalWrite(LED_ERROR_VERDE, LOW);

    // Inicializa el Bluetooth BLE
    BLEDevice::init("SerialScope Pruebas Master");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    BLEService *pService = pServer->createService(SERVICE_UUID);
    pTxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
    pTxCharacteristic->addDescriptor(new BLE2902());
    BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
    pRxCharacteristic->setCallbacks(new MyCallbacks());
    pService->start();
    pServer->getAdvertising()->start();
    Serial.println("Sistema de Pruebas listo.");
    delay(500); // Espera a que el sensor MAX6675 se estabilice
}

// Función para procesar las tareas recibidas por BLE
void processTasks() {
    if (modoActual == SPI_EMU) {
        if (spiProfile == "MAX6675" && millis() - ultimaTarea > 1000) {
            float celsius = thermocouple.readCelsius();
            String res;
            if (isnan(celsius)) {
                res = "Error: Sin Sensor";
                digitalWrite(LED_ERROR_VERDE, HIGH);
            } else if (celsius == 0.00) {
                res = "Aviso: Lectura 0.00 (Revisar Cables)";
                digitalWrite(LED_ERROR_VERDE, HIGH);
            } else {
                res = "Temperatura: " + String(celsius) + " C";
                digitalWrite(LED_ERROR_VERDE, LOW);
            }
            Serial.println("[SPI] " + res);
            bleSend("RESULTADO: " + res);
            ultimaTarea = millis();
        }
    } else if (modoActual == SPI_EMU && spiProfile == "TFT") {
        // En modo TFT no hacemos spam de lectura, esperamos comandos EMU_MSG
    } else if (modoActual == I2C_EMU) {
        if (millis() - ultimaTarea > 2000) {
            String msg = "";
            
            if (i2cProfile == "BME") {
                // Tráfico I2C Real para BMP180
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
                        msg = "Dato del Sensor (0x" + String(i2cAddress, HEX) + "): " + String(val);
                    }
                } else {
                    msg = "NACK: BMP180 no detectado en 0x" + String(i2cAddress, HEX);
                    digitalWrite(LED_ERROR_VERDE, HIGH);
                }
            } else if (i2cProfile == "TMP") {
                // Tráfico I2C Real para TMP102 con Filtro de Promedio debido a la sensibilidad de corriente  a pesar que s epuso capacitores (5 muestras)
                int acumulado = 0;
                int muestrasOk = 0;
                
                for (int i = 0; i < 5; i++) {
                    Wire.beginTransmission(i2cAddress);
                    Wire.write(0x00);
                    if (Wire.endTransmission() == 0) {
                        Wire.requestFrom((uint16_t)i2cAddress, (uint8_t)2);
                        if (Wire.available() == 2) {
                            uint8_t msb = Wire.read();
                            uint8_t lsb = Wire.read();
                            acumulado += ((msb << 8) | lsb) >> 4;
                            muestrasOk++;
                        }
                    }
                    delay(20); // Pequeña pausa entre muestras
                }

                if (muestrasOk > 0) {
                    int val = acumulado / muestrasOk;
                    msg = "Lectura Digital TMP102 (0x" + String(i2cAddress, HEX) + "): " + String(val);
                } else {
                    msg = "NACK: TMP102 no detectado en 0x" + String(i2cAddress, HEX);
                    digitalWrite(LED_ERROR_VERDE, HIGH);
                }
            }
            
            Serial.println("[I2C] " + msg);
            bleSend("RESULTADO: " + msg);
            ultimaTarea = millis();
        }
    }
}

void loop() {
    // Detectar desconexión de BLE y reiniciar advertising
    if (!deviceConnected && oldDeviceConnected) { delay(500); pServer->startAdvertising(); oldDeviceConnected = deviceConnected; }
    if (deviceConnected && !oldDeviceConnected) { oldDeviceConnected = deviceConnected; }
    
    // Lógica del LED Blanco
    if (!deviceConnected) {
        // Parpadea si no hay clientes BLE conectados
        if (millis() - lastBlinkTime > 500) {
            blinkState = !blinkState;
            digitalWrite(LED_BLE_BLANCO, blinkState ? HIGH : LOW);
            lastBlinkTime = millis();
        }
    } else {
        // Si acaba de conectarse, se prende fijo por 2 segundos y luego se apaga
        if (oldDeviceConnected && (millis() - lastBlinkTime > 2000)) {
            digitalWrite(LED_BLE_BLANCO, LOW);
        } else if (!oldDeviceConnected) {
            // Recién conectado
            digitalWrite(LED_BLE_BLANCO, HIGH);
            lastBlinkTime = millis(); 
        }
    }

    // Apagar temporalmente el LED verde tras un PONG
    if (greenLedTurnOffTime > 0 && millis() > greenLedTurnOffTime) {
        digitalWrite(LED_ERROR_VERDE, LOW);
        greenLedTurnOffTime = 0;
    }

    // Lógica de parpadeo de LEDs de Protocolos
    if (modoActual != IDLE && ledBlinkTarget > 0) {
        unsigned long currentMillis = millis();
        int activePin = (modoActual == UART_EMU) ? LED_MODO_ROJO : (modoActual == I2C_EMU ? LED_MODO_AZUL : LED_MODO_AMARILLO);
        
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
        Serial.print("[UART RX Slave] "); Serial.print(uartIncoming);
        bleSend("Recibido: " + uartIncoming);
    }

    delay(10);
}

void ejecutarComando(String cmd) {
    // Verifica si el comando es PING
    if (cmd == "PING") { 
        Serial.println("[SISTEMA] >>> PING recibido... respondiendo PONG (LED Verde)"); 
        bleSend("PONG"); 
        digitalWrite(LED_ERROR_VERDE, HIGH);
        greenLedTurnOffTime = millis() + 500;
        return; 
    }
    
    // Verifica si el comando es EMU_STOP, si es asi detiene la emulacion (BOTON DE DETENER EN LA WEB)
    if (cmd == "EMU_STOP") { 
        emulacionActiva = false;
        Serial.println("[SISTEMA] >>> Prueba detenida.");
        bleSend("STOP_OK"); 
        return; 
    }

    if (cmd.startsWith("EMU_CONFIG:")) {
        // Formatos:
        // EMU_CONFIG:UART:AUTO:115200:GPS
        // EMU_CONFIG:I2C:AUTO:0x08:100000:ACCEL
        // EMU_CONFIG:SPI:AUTO:0:FLASH
        String tokens[6];
        int count = 0;
        int startIndex = 0;
        for (int i = 0; i <= cmd.length(); i++) {
            if (cmd.charAt(i) == ':' || i == cmd.length()) {
                tokens[count++] = cmd.substring(startIndex, i);
                startIndex = i + 1;
                if (count >= 6) break;
            }
        }
        
        String proto = tokens[1];
        modoActual = IDLE;

        // Verifica si el protocolo es UART, si es asi configura el baudrate y el profile
        if (proto == "UART") {
            long baud = tokens[3].toInt();
            String profile = tokens[4];
            int blinks = 1;
            if (baud == 19200) blinks = 2;
            else if (baud == 38400) blinks = 3;
            else if (baud == 57600) blinks = 4;
            else if (baud == 115200) blinks = 5;

            Serial2.end();
            Serial2.begin(baud, SERIAL_8N1, 16, 17);
            actualizarLeds(UART_EMU, blinks);
            contadorUART = 0;
            digitalWrite(LED_ERROR_VERDE, LOW);

            Serial.print("[SISTEMA] >>> Configurando UART | Baud: "); Serial.print(baud);
            Serial.print(" | Perfil: "); Serial.println(profile);

            bleSend("EMU_OK");
            bleSend("RESULTADO: Iniciando envio UART a " + String(baud) + " baud...");

        // Verifica si el protocolo es I2C, si es asi configura la velocidad y el profile
        } else if (proto == "I2C") {
            long speed = tokens[4].toInt();
            int blinks = (speed == 400000) ? 2 : 1;
            
            String addrStr = tokens[3];
            addrStr.replace("0x", "");
            i2cAddress = strtol(addrStr.c_str(), NULL, 16);
            if (i2cAddress == 0) i2cAddress = 0x77; // Fallback
            
            i2cProfile = tokens[5];

            Wire.begin(); // Inicializar como maestro I2C
            Wire.setClock(speed);
            
            actualizarLeds(I2C_EMU, blinks);
            digitalWrite(LED_ERROR_VERDE, LOW);

            Serial.print("[SISTEMA] >>> Configurando I2C | Addr: 0x"); Serial.print(String(i2cAddress, HEX));
            Serial.print(" | Speed: "); Serial.print(speed/1000); 
            Serial.print("kHz | Perfil: "); Serial.println(i2cProfile);

            bleSend("EMU_OK");
            bleSend("RESULTADO: Iniciando sondeo (" + i2cProfile + ") en 0x" + String(i2cAddress, HEX) + " a " + String(speed/1000) + " kHz...");

        // Verifica si el protocolo es SPI, si es asi configura el modo y el profile
        } else if (proto == "SPI") {
            int modeIdx = tokens[3].toInt();
            spiProfile = tokens[4]; // Corregido: asignar a la variable global
            int blinks = modeIdx + 1;

            actualizarLeds(SPI_EMU, blinks);
            
            if (spiProfile == "TFT") {
                Serial.println("[SISTEMA] Inicializando Pantalla TFT...");
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

            Serial.print("[SISTEMA] >>> Configurando SPI | Modo: "); Serial.print(modeIdx);
            Serial.print(" | Perfil: "); Serial.println(spiProfile);

            bleSend("EMU_OK");
            String resMsg = (spiProfile == "TFT") ? "Pantalla TFT Lista" : "Leyendo sensor MAX6675";
            bleSend("RESULTADO: " + resMsg + " en SPI Modo " + String(modeIdx) + "...");
        }
        ultimaTarea = millis() - 2000; // Forzar ejecución inmediata
        emulacionActiva = false; // No iniciar spam hasta recibir EMU_START
    } else if (cmd == "EMU_START") {
        emulacionActiva = true;
        Serial.println("[SISTEMA] >>> Reanudando prueba...");
        bleSend("EMU_OK");
    } else if (cmd.startsWith("EMU_MSG:")) {
        int f = cmd.indexOf(':');
        int s = cmd.indexOf(':', f+1);
        String p = cmd.substring(f+1, s);
        String data = cmd.substring(s+1);
        
        if (p == "UART" && modoActual == UART_EMU) {
            Serial2.print(data);
            bleSend("Enviado: " + data);
            digitalWrite(LED_ERROR_VERDE, HIGH);
            greenLedTurnOffTime = millis() + 200;
        } else if (p == "I2C") {
            // Caso 1: Lectura de calibración (ya existente)
            if (data == "READ_CALIB") {
                Serial.println("\n[SISTEMA] >>> Solicitando tabla de calibración al BMP180 (Addr: 0x77)...");
                Wire.beginTransmission(0x77);
                Wire.write(0xAA);
                if (Wire.endTransmission() != 0) {
                    Serial.println("[ERROR] No se detecta el sensor en 0x77.");
                    bleSend("RESULTADO:Error: Sensor NACK");
                    return;
                }
                Wire.requestFrom(0x77, 22);
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
                    digitalWrite(LED_ERROR_VERDE, HIGH);
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
            delay(15); // Pausa mínima para estabilidad del stack BLE
        }
    }
}
