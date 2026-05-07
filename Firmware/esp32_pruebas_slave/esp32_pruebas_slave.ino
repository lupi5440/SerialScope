/*
 * =====================================================================================
 *  PROYECTO : SerialScope — Módulo de Pruebas (Slave)
 *  ARCHIVO  : esp32_pruebas_slave.ino
 *  AUTOR    : Juan Angel Serrano Carreño
 * =====================================================================================
 *
 *  DESCRIPCIÓN:
 *    Firmware del ESP32 Slave del banco de pruebas. Actúa como el extremo receptor
 *    Recibe datos por UART/RS-232 y los notifica al navegador vía Bluetooth (BLE).
 *    Permite probar que la comunicación Serial es íntegra y bidireccional.
 *
 *  MECANISMO:
 *    1. Escucha comandos BLE (PING, CFG_BAUD).
 *    2. Actúa como puente: Lo que recibe de la web va al UART, y viceversa.
 *
 * =====================================================================================
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define SERVICE_UUID           "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_RX "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_TX "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// LEDs
#define LED_MODO_ROJO 25
#define LED_BLE_BLANCO 32
#define LED_ERROR_VERDE 33

// Variables LEDs
unsigned long lastBlinkTime = 0;
bool blinkState = false;
unsigned long greenLedTurnOffTime = 0;

int ledBlinkTarget = 0;
int ledBlinkCount = 0;
bool ledBlinkState = false;
unsigned long lastProtocolLedTime = 0;
bool ledResting = false;

//Configuracion de blink para el LED Rojo depende del baudrate
void actualizarBlinks(long newBaud) {
    if (newBaud == 9600) ledBlinkTarget = 1;
    else if (newBaud == 19200) ledBlinkTarget = 2;
    else if (newBaud == 38400) ledBlinkTarget = 3;
    else if (newBaud == 57600) ledBlinkTarget = 4;
    else if (newBaud == 115200) ledBlinkTarget = 5;
    else ledBlinkTarget = 0;
    
    ledBlinkCount = 0;
    ledBlinkState = false;
    ledResting = false;
    lastProtocolLedTime = millis();
}

// Clase para manejar las conexiones BLE
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) { 
        deviceConnected = true; 
        digitalWrite(LED_BLE_BLANCO, HIGH);
        Serial.println("Bluetooth Conectado (Slave)");
        lastBlinkTime = millis();
    }
    void onDisconnect(BLEServer* pServer) { 
        deviceConnected = false; 
        digitalWrite(LED_BLE_BLANCO, LOW);
        digitalWrite(LED_MODO_ROJO, LOW);
        Serial.println("Bluetooth Desconectado (Slave)");
        ledBlinkTarget = 0;
    }
};

//Clase para manejar las peticiones de escritura desde el navegador (Web -> UART)
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        String rxValue = pCharacteristic->getValue();
        if (rxValue.length() > 0) {
            rxValue.trim();
            
            //Comando para cambiar velocidad UART remotamente
            if (rxValue.startsWith("CFG_BAUD:")) {
                long newBaud = rxValue.substring(9).toInt();
                if (newBaud > 0) {
                    Serial2.end();
                    Serial2.begin(newBaud, SERIAL_8N1, 16, 17);
                    actualizarBlinks(newBaud);
                    Serial.printf("[BLE] Baudrate cambiado a %ld\n", newBaud);
                }
            } 

            // Comando PING para prueba de enlace
            else if (rxValue == "PING") {
                digitalWrite(LED_ERROR_VERDE, HIGH);
                greenLedTurnOffTime = millis() + 500;
                String response = "PONG_DESTINO\n";
                if(deviceConnected) {
                    pTxCharacteristic->setValue((uint8_t*)response.c_str(), response.length());
                    pTxCharacteristic->notify();
                }
            } 

            //Reenvío de texto plano al Master por UART
            else {
                Serial2.print(rxValue);
                //Confirmación local en monitor serial
                Serial.print("[BLE -> UART] "); Serial.println(rxValue);
            }
        }
    }
};

void setup() {
    Serial.begin(115200);
    //Configuración UART destino (Pines 16=RX, 17=TX)
    Serial2.begin(115200, SERIAL_8N1, 16, 17);
    actualizarBlinks(115200);
    
    pinMode(LED_MODO_ROJO, OUTPUT);
    pinMode(LED_BLE_BLANCO, OUTPUT);
    pinMode(LED_ERROR_VERDE, OUTPUT);
    digitalWrite(LED_MODO_ROJO, LOW);
    digitalWrite(LED_BLE_BLANCO, LOW);
    digitalWrite(LED_ERROR_VERDE, LOW);

    //Inicialización del dispositivo BLE
    BLEDevice::init("SerialScope Pruebas Slave");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    BLEService *pService = pServer->createService(SERVICE_UUID);
    pTxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
    pTxCharacteristic->addDescriptor(new BLE2902());
    BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
    pRxCharacteristic->setCallbacks(new MyCallbacks());
    pService->start();
    pServer->getAdvertising()->start();
    Serial.println("ESP32 Slave UART listo.");
}

void loop() {

    //Mantiene la conexión BLE activa
    if (!deviceConnected && oldDeviceConnected) { 
        delay(500); 
        pServer->startAdvertising(); 
        oldDeviceConnected = deviceConnected; 
    }
    if (deviceConnected && !oldDeviceConnected) { 
        oldDeviceConnected = deviceConnected; 
    }

    // Lógica del LED Blanco
    if (!deviceConnected) {
        if (millis() - lastBlinkTime > 500) {
            blinkState = !blinkState;
            digitalWrite(LED_BLE_BLANCO, blinkState ? HIGH : LOW);
            lastBlinkTime = millis();
        }
    } else {
        if (millis() - lastBlinkTime > 2000) {
            digitalWrite(LED_BLE_BLANCO, LOW);
        }
    }

    //Lógica LED Verde (PING)
    if (greenLedTurnOffTime > 0 && millis() > greenLedTurnOffTime) {
        digitalWrite(LED_ERROR_VERDE, LOW);
        greenLedTurnOffTime = 0;
    }

    // Lógica LED Rojo (Parpadeo UART)
    if (deviceConnected && ledBlinkTarget > 0) {
        unsigned long currentMillis = millis();
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
                digitalWrite(LED_MODO_ROJO, ledBlinkState ? HIGH : LOW);
                
                if (!ledBlinkState) {
                    ledBlinkCount++;
                    if (ledBlinkCount >= ledBlinkTarget) {
                        ledResting = true;
                        digitalWrite(LED_MODO_ROJO, LOW);
                    }
                }
            }
        }
    } else if (!deviceConnected) {
        digitalWrite(LED_MODO_ROJO, LOW);
    }

    // PUENTE UART -> BLE (Con cortador de mensajes largos para evitar errores por saturación de memoria)
    // Leemos lo que llega del Master por el cable UART y lo mandamos a la web por Bluetooth
    if (Serial2.available()) {
        String uartData = "";
        // Leemos el buffer disponible
        while(Serial2.available()) {
            uartData += (char)Serial2.read();
            if (uartData.length() >= 256) break; // Límite de seguridad
        }
        
        Serial.print("[UART -> BLE] "); Serial.print(uartData);
        
        if (deviceConnected) {
            // Dividimos en trozos de 20 bytes (estándar BLE) para asegurar que llegue todo el texto
            int dataLen = uartData.length();
            for (int i = 0; i < dataLen; i += 20) {
                int endIdx = (i + 20 < dataLen) ? (i + 20) : dataLen;
                String chunk = uartData.substring(i, endIdx);
                
                pTxCharacteristic->setValue((uint8_t*)chunk.c_str(), chunk.length());
                pTxCharacteristic->notify();
                delay(10); // Pequeña pausa para que el stack BLE procese el paquete
            }
        }
    }
}
