/*
 * =====================================================================================
 *  PROYECTO : SerialScope — Módulo de Pruebas (Slave)
 *  ARCHIVO  : esp32_pruebas_slave.ino
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
 *  CONEXIÓN UART (RS-232):
 *    - RX: GPIO 16
 *    - TX: GPIO 17
 *    - Velocidad inicial: 115200 baudios
 * 
 *  INDICADORES:
 *    - LED Verde (13): Se enciende por X tiempo cada vez que llega un PING
 *    - LED Blanco (4): Parpadea cuando está desconectado, fijo cuando está conectado
 *    - LED Rojo (14): Muestra la velocidad del puerto Serial con parpadeos
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

// Variables BLE
BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// --- TIEMPOS Y PARÁMETROS DEL SISTEMA (Milisegundos) ---
#define BLE_BLINK_INTERVAL_MS   500   // Parpadeo cuando busca conexión BLE
#define BLE_CHUNK_SIZE          20    // Tamaño máximo del paquete de envío Bluetooth
#define BLE_MTU                 512   // Tamaño máximo de negociación Bluetooth
#define PING_LED_DURATION_MS    500   // Tiempo que el LED verde se queda prendido con un PING
#define UART_BLINK_INTERVAL_MS  200   // Velocidad del parpadeo rojo (indicador de baudios)
#define UART_BLINK_REST_MS      1500  // Pausa entre la secuencia de parpadeos rojos
#define UART_MAX_RX_BUFFER      256   // Límite de seguridad para lectura de UART

// LEDs de control 
#define LED_STATUS_VERDE  13  
#define LED_BLE_BLANCO    4   
#define LED_UART_ROJO     14  

// --- PINES DE HARDWARE ---
#define UART_RX_PIN       16  // Pin de recepción Serial2
#define UART_TX_PIN       17  // Pin de transmisión Serial2

// Variables para el LED de estado Bluetooth (Blanco)
unsigned long bleStatusLastToggleTime = 0;
bool bleLedCurrentState = false;

// Temporizador para LED verde
unsigned long pingIndicatorOffTime = 0;

// Variables para el indicador visual de Baudrate (LED Rojo)
int baudIndicatorTotalBlinks = 0;
int baudIndicatorCurrentBlinks = 0;
bool baudLedCurrentState = false;
unsigned long baudLedLastToggleTime = 0;
bool baudLedInRestPeriod = false;

//Configuracion de blink para el LED Rojo depende del baudrate
void actualizarBlinks(long newBaud) {
    switch (newBaud) {
        case 9600:   baudIndicatorTotalBlinks = 1; break;
        case 19200:  baudIndicatorTotalBlinks = 2; break;
        case 38400:  baudIndicatorTotalBlinks = 3; break;
        case 57600:  baudIndicatorTotalBlinks = 4; break;
        case 115200: baudIndicatorTotalBlinks = 5; break;
        default:     baudIndicatorTotalBlinks = 0; break; 
    }
    baudIndicatorCurrentBlinks = 0;
    baudLedCurrentState = false;
    baudLedInRestPeriod = false;
    baudLedLastToggleTime = millis();
}

// Clase para manejar las conexiones BLE
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) { 
        deviceConnected = true; 
        digitalWrite(LED_BLE_BLANCO, HIGH);
        digitalWrite(LED_UART_ROJO, LOW);   
        digitalWrite(LED_STATUS_VERDE, LOW); 
        Serial.println("[BLE] >>> Bluetooth conectado (Slave)");
        bleStatusLastToggleTime = millis();
    }
    void onDisconnect(BLEServer* pServer) { 
        deviceConnected = false; 
        digitalWrite(LED_BLE_BLANCO, LOW);
        digitalWrite(LED_UART_ROJO, LOW);
        digitalWrite(LED_STATUS_VERDE, LOW);
        Serial.println("[BLE] >>> Bluetooth desconectado (Slave)");
        baudIndicatorTotalBlinks = 0;
    }
};

//Clase para manejar las peticiones de escritura desde el navegador (Web -> UART)
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        String rxValue = String(pCharacteristic->getValue().c_str());  //Forzar conversion de estandar C++ para la lib BLE mas compatible
        if (rxValue.length() > 0) {
            rxValue.trim();
            
            //Comando para cambiar velocidad UART remotamente
            if (rxValue.startsWith("CFG_BAUD:")) {
                long newBaud = rxValue.substring(9).toInt();
                if (newBaud > 0) {
                    Serial2.flush(); // Espera a que termine de salir cualquier dato pendiente para hacer el cambio
                    Serial2.end();
                    Serial2.begin(newBaud, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
                    actualizarBlinks(newBaud);
                    Serial.printf("[CONFIG] >>> Baudrate cambiado a %ld\n", newBaud);
                }
            } 

            // Comando PING para prueba de enlace y latencia
            else if (rxValue.startsWith("PING")) {
                digitalWrite(LED_STATUS_VERDE, HIGH);
                pingIndicatorOffTime = millis() + PING_LED_DURATION_MS;
                
                String response = "PONG";
                int separatorIdx = rxValue.indexOf(':');
                if (separatorIdx != -1) {
                    response += ":" + rxValue.substring(separatorIdx + 1);
                }
                response += "\n";
                
                if(deviceConnected) {
                    pTxCharacteristic->setValue((uint8_t*)response.c_str(), response.length());
                    pTxCharacteristic->notify();
                }
            }

            //Reenvío de texto plano al Master por UART
            else {
                Serial2.print(rxValue);
                Serial.printf("[BLE -> UART] %s\n", rxValue.c_str());
            }
        }
    }
};

void setup() {
    Serial.begin(115200);
    //Configuración UART destino (Pines 16=RX, 17=TX preferidos)
    Serial2.begin(115200, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
    actualizarBlinks(115200);
    
    //Definición de pines de salida para LEDs
    pinMode(LED_UART_ROJO, OUTPUT);
    pinMode(LED_BLE_BLANCO, OUTPUT);
    pinMode(LED_STATUS_VERDE, OUTPUT);

    digitalWrite(LED_UART_ROJO, LOW);
    digitalWrite(LED_BLE_BLANCO, LOW);
    digitalWrite(LED_STATUS_VERDE, LOW);

    //Inicialización del dispositivo BLE
    BLEDevice::init("SerialScope Slave"); // Nombre del dispositivo
    BLEDevice::setMTU(BLE_MTU); 
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

    // Se conecta al dispositivo BLE
    if (deviceConnected && !oldDeviceConnected) { 
        oldDeviceConnected = deviceConnected; 
    }

    // Logica del LED Blanco (Estado de Conexión BLE)
    if (!deviceConnected) {
        
        // Si esta desconectado parpadea el led blanco
        if (millis() - bleStatusLastToggleTime > BLE_BLINK_INTERVAL_MS) {
            bleLedCurrentState = !bleLedCurrentState;
            digitalWrite(LED_BLE_BLANCO, bleLedCurrentState ? HIGH : LOW);
            bleStatusLastToggleTime = millis();
        }

    } else {

        // LED blanco fijo indica conexión BLE activa
        digitalWrite(LED_BLE_BLANCO, HIGH);
    }

    // Logica LED Verde (Indicador de PING)
    if (pingIndicatorOffTime > 0 && millis() > pingIndicatorOffTime) {
        digitalWrite(LED_STATUS_VERDE, LOW);
        pingIndicatorOffTime = 0;
    }

    // Logica LED Rojo (Indicador de Baudrate UART)
    if (deviceConnected && baudIndicatorTotalBlinks > 0) {
        unsigned long currentMillis = millis();
        if (baudLedInRestPeriod) {
            // Descanso entre secuencias de blink
            if (currentMillis - baudLedLastToggleTime > UART_BLINK_REST_MS) {
                baudLedInRestPeriod = false;
                baudIndicatorCurrentBlinks = 0;
                baudLedLastToggleTime = currentMillis;
            }
        } else {
            // Parpadea el led rojo 
            if (currentMillis - baudLedLastToggleTime > UART_BLINK_INTERVAL_MS) {
                baudLedLastToggleTime = currentMillis;
                baudLedCurrentState = !baudLedCurrentState;
                digitalWrite(LED_UART_ROJO, baudLedCurrentState ? HIGH : LOW);
                // Cuando se apaga el led rojo, se incrementa el contador de parpadeos
                if (!baudLedCurrentState) {
                    baudIndicatorCurrentBlinks++;
                    // Cuando se completan todos los parpadeos, se reinicia el contador
                    if (baudIndicatorCurrentBlinks >= baudIndicatorTotalBlinks) {
                        baudLedInRestPeriod = true;
                        digitalWrite(LED_UART_ROJO, LOW);
                    }
                }
            }
        }
    } else if (!deviceConnected) {
        // Si esta desconectado se apaga el led rojo
        digitalWrite(LED_UART_ROJO, LOW);
    }

    // UART -> BLE (Con cortador de mensajes largos para evitar errores por saturación de memoria)
    // Leemos lo que llega del Master por el cable UART y lo mandamos a la web por Bluetooth
    if (Serial2.available()) {
        uint8_t rxBuffer[UART_MAX_RX_BUFFER];
        int bytesRead = 0;

        // Leemos el buffer disponible hacia la memoria estática
        while(Serial2.available() && bytesRead < UART_MAX_RX_BUFFER) {
            rxBuffer[bytesRead++] = Serial2.read();
        }
        
        if (deviceConnected) {
            digitalWrite(LED_STATUS_VERDE, LOW); // Apaga led de error

            // Dividimos en trozos (Chunks) de 20 bytes para BLE
            for (int i = 0; i < bytesRead; i += BLE_CHUNK_SIZE) {
                int currentChunkSize = (i + BLE_CHUNK_SIZE < bytesRead) ? BLE_CHUNK_SIZE : (bytesRead - i);
                
                pTxCharacteristic->setValue(rxBuffer + i, currentChunkSize);
                pTxCharacteristic->notify();
                
                // Reducimos el delay a 3ms; suficiente para que el stack BLE respire
                // sin generar un cuello de botella en lecturas largas
                delay(3); 
            }
        } else {
            // ERROR: Se reciben datos por cable pero no hay conexión BLE
            digitalWrite(LED_STATUS_VERDE, HIGH);
        }
    }
}
