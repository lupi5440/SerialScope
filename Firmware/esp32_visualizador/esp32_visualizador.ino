/*
 * =====================================================================================
 *  PROYECTO : SerialScope — Visualizador (Sniffer / Proxy)
 *  ARCHIVO  : esp32_visualizador.ino
 * =====================================================================================
 *
 *  DESCRIPCIÓN:
 *    Firmware del ESP32 Visualizador. Captura tráfico de bus y lo retransmite a la interfaz web vía WebSocket sobre WiFi.
 *
 *  MODOS DE OPERACIÓN:
 *    - UART (Proxy)        : Todo el trafico recibido por el canal serial 1 lo reenvia al canal serial 2  y viceversa
 *    - I²C  (Sniffer)      : Recibe por interrupcion todo el trafico de I2C y lo muestra en la web
 *    - I²C  (Maestro)      : Se comporta como un maestro I2C y puede configurarse para pedir datos a un esclavo 
 *    - I²C  (Esclavo)      : Se comporta como un esclavo I2C y puede configurarse para recibir datos de un maestro 
 *    - SPI  (Sniffer)      : Recibe por interrupcion todo el trafico de SPI y lo muestra en la web
 *    - SPI  (Maestro)      : Se comporta como un maestro SPI y puede configurarse para pedir datos a un esclavo 
 *    - SPI  (Esclavo)      : Se comporta como un esclavo SPI y puede configurarse para recibir datos de un maestro 
 *
 *  PINOUT SEGURO:
 *    LEDs     → GPIO 13 (Verde-Status), 4 (Blanco-wifi), 14 (Rojo-UART), 27 (Amarillo-I2C), 26 (Azul-SPI), 25 (Frecuencia-SPI)  
 *    UART     → Serial1 rempapeado: RX:33, TX:32  |  Serial2: RX:17, TX:16
 *    I²C      → SCL:22, SDA:21 
 *    SPI      → SCK:18, MISO:19, MOSI:23, CS:5
 *
 *  RED WiFi:          "SerialScope_Visualizador"              Password: 12345
 *  WebSocket:         ws://192.168.4.1                        Puerto:80
 *
 * =====================================================================================
 */

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Wire.h>
#include <SPI.h>
#include "soc/soc.h"           // Librería para control de registros
#include "soc/rtc_cntl_reg.h"  // Librería para control de Brownout (para que si hay poca corriente no se reinicie)


// ****************************************************************************************
//                                       Red        
// ****************************************************************************************

const char* ssid = "SerialScope_Visualizador";
const char* password = "12345678";  //Minimo debe tener 8 caracteres
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ****************************************************************************************
//                                       LEDs      
// ****************************************************************************************

// --- PINES PARA LEDs INDICADORES ---
#define LED_STATUS_VERDE   13  // Verde (Status)
#define LED_WIFI_BLANCO    4   // Blanco (WiFi)
#define LED_UART_ROJO      14  // Rojo (UART)
#define LED_I2C_AMARILLO   27  // Amarillo (I2C)
#define LED_SPI_AZUL       26  // Azul (SPI)
#define LED_SPI_FREC_AZUL  25  // AZUL Frecuencia (SPI)

// --- CONTROL DE PARPADEO: LEDs DE PROTOCOLO ---
uint8_t ledBlinkTarget = 0;              // uint8_t para ahorrar memoria
uint8_t ledBlinkCount = 0;
bool ledBlinkState = false;
bool ledResting = false;
unsigned long lastProtocolLedTime = 0;   // Temporizador para el parpadeo

// --- CONTROL DE PARPADEO: LED FRECUENCIA SPI ---
uint8_t freqBlinkTarget = 0;            
uint8_t freqBlinkCount = 0;
bool freqBlinkState = false;
bool freqResting = false;
unsigned long lastFreqLedTime = 0;       // Temporizador para la frecuencia

// --- CONTROL: LEDs DE ESTADO Y WIFI ---
bool blinkState = false;                 // Estado del parpadeo general (WiFi)
unsigned long lastBlinkTime = 0;         // Temporizador general (WiFi)
unsigned long whiteLedOffTime = 0; 
unsigned long greenLedTurnOffTime = 0;

// --- TEMPORIZADORES INDIVIDUALES
unsigned long redLedOffTime = 0;
unsigned long blueLedOffTime = 0; 
unsigned long lastFlushTime = 0;       


// ****************************************************************************************
//                              Estado global      
// ****************************************************************************************

// Protocolo actual
String currentProtocol = "";

// Instancia para recervar pines SPI 
SPIClass SPI_Sensing(VSPI);

// Definicion de modos SPI y Roles para los protocolos SPI e I2C
uint8_t spi_modes[] = {SPI_MODE0, SPI_MODE1, SPI_MODE2, SPI_MODE3};
enum Role { ROLE_SNIFFER, ROLE_MASTER, ROLE_SLAVE };  

// Intervalo de tiempo en milisegundos para enviar los datos por WiFi y evitar saturación
const int FLUSH_INTERVAL = 20;  

//Variable para saber si el ESP está en alguna configuración.
volatile bool estaConfigurando = false;

// Funcion para enviar texto al WebSocket
void wsSend(String texto);

// Estructura para enviar por la cola
typedef struct {
    char cmd[128]; // Soporta comandos de hasta 127 caracteres
} MensajeComando;

// Identificador de la cola de FreeRTOS
QueueHandle_t colaComandos;

// ****************************************************************************************
//                                       UART  
// ****************************************************************************************

//Buffer para UART
String bufferM2S = "";
String bufferS2M = "";

// ****************************************************************************************
//                                    Interrupciones  
// ****************************************************************************************

// Declaraciones de interrupciones (ISRs para i2c y spi)
void IRAM_ATTR onSCLRising();
void IRAM_ATTR onSDAChange();
void IRAM_ATTR onSCKRising();

Role i2cRole = ROLE_SNIFFER;
Role spiRole = ROLE_SNIFFER;

// ****************************************************************************************
//                                       I2C      
// ****************************************************************************************

 // --- Parametros de configuracion I2C ---
long activeI2CSpeed = 100000;
int activeI2CAddr = 0x08;
int activeI2CReg = 0x00;

 // --- Varibles de snifing I2C ---
 #define I2C_BUFFER_SIZE 1024
 volatile uint8_t i2cRawBuffer[I2C_BUFFER_SIZE]; // Almacén circular para los bytes capturados
 volatile int i2cWriteIdx = 0;                   // Dónde escribe el sniffer (interrupciones)
 volatile int i2cReadIdx = 0;                    // Dónde lee el programa principal (loop)
 volatile unsigned long lastI2cTime = 0;         // Marca de tiempo para detectar fin de ráfaga
 volatile uint8_t i2cCurrentByte = 0;            // Byte que se está construyendo bit a bit
 volatile bool i2cStarted = false;               // Para pausar la captura de datos 
volatile int i2cBitCount = 0;                    // Contador de bits para identificar si es dirección o data
 
 // Lectura de interrupcion de registros para I2C a nivel de hardware
 #define READ_SCL_FAST ((REG_READ(GPIO_IN_REG) >> 22) & 1)
 #define READ_SDA_FAST ((REG_READ(GPIO_IN_REG) >> 21) & 1)

// --- Variables de configuracion modo esclavo I2C con 4 bytes de almacenamiento---
bool spiSlaveMode = false;
int spiSlaveBytePtr = 0;  //indice inicio registro byte
int spiSlaveBitPtr = 0;   //indice bit
uint8_t spiSlaveBuffer[256]; //buffer 4 registros
int spiSlaveBufferSize = 0;

// ****************************************************************************************
//                                       SPI      
// ****************************************************************************************


// --- Parametros de configuracion SPI ---
long activeSPIFreq = 1000000;
int activeSPIMode = 0;

// --- Variables de estado SPI ---
 #define SPI_BUFFER_SIZE 512
 volatile uint8_t spiRawBuffer[SPI_BUFFER_SIZE];  //Buffer circular para SPI
 volatile int spiWriteIdx = 0;                    //Volatile para que la interrupción pueda acceder a ella
 volatile int spiReadIdx = 0;                     //Donde lee el programa principal
 volatile int spiBitCnt = 0;                      //Contador de bits para identificar si es dirección o data
 
// buffer temporal para recibir datos 
 volatile uint8_t spiByterx = 0;
 volatile uint8_t spiBytetx = 0;
 volatile unsigned long lastSpiTime = 0;

 // Lectura de registros para SPI (Acceso directo al hardware del ESP32)
 #define READ_MOSI_FAST ((REG_READ(GPIO_IN_REG) >> 23) & 1)
 #define READ_MISO_FAST ((REG_READ(GPIO_IN_REG) >> 19) & 1)
 #define READ_CS_FAST   ((REG_READ(GPIO_IN_REG) >> 5) & 1)

// ****************************************************************************************
//                                    SETUP PRINCIPAL          
// ****************************************************************************************

void setup() {
    
    // Inicialización de comunicación serial
    Serial.begin(115200);
    
    // DESACTIVAR BROWNOUT DETECTOR (Para evitar reinicios por caídas leves de voltaje)
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
    
    delay(2000); // Esperar a que el voltaje se estabilice tras el encendido

    // Configuración de Pines Seriales 
    Serial1.begin(115200, SERIAL_8N1, 16, 17); // RX: 16, TX: 17 (Nativo UART2)
    Serial2.begin(115200, SERIAL_8N1, 33, 32); // RX: 33, TX: 32 (Remapeado de UART1 puede llevar lentitud)
    
    // Cola de comando para evitar condicion de carrera entre loop y web socket
    colaComandos = xQueueCreate(10, sizeof(MensajeComando));
    if(colaComandos == NULL){
        Serial.println("[Error] >>> No se pudo crear la cola de comandos");
    }

    // Configuración de Pines SPI 
    SPI_Sensing.begin(18, 19, 23, 5);

    // Configurar interrupciones I2C 
    pinMode(21, INPUT_PULLUP);
    pinMode(22, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(22), onSCLRising, RISING);
    attachInterrupt(digitalPinToInterrupt(21), onSDAChange, CHANGE);

    // Configuración de pines para LEDs
    pinMode(LED_UART_ROJO, OUTPUT);
    pinMode(LED_SPI_AZUL, OUTPUT);
    pinMode(LED_I2C_AMARILLO, OUTPUT);
    pinMode(LED_WIFI_BLANCO, OUTPUT);
    pinMode(LED_STATUS_VERDE, OUTPUT);
    pinMode(LED_SPI_FREC_AZUL, OUTPUT);
    
    // Estado inicial de LEDs 
    digitalWrite(LED_WIFI_BLANCO, LOW);
    digitalWrite(LED_STATUS_VERDE, LOW);
    digitalWrite(LED_SPI_FREC_AZUL, LOW);
    digitalWrite(LED_UART_ROJO, LOW);
    digitalWrite(LED_SPI_AZUL, LOW);
    digitalWrite(LED_I2C_AMARILLO, LOW);
    
    // Configura el punto de acceso Wi-Fi
    WiFi.softAP(ssid, password);
    digitalWrite(LED_WIFI_BLANCO, HIGH);
    
    // Inicia el servidor web para manejar la comunicación con la interfaz web
    ws.onEvent(onEvent);
    server.addHandler(&ws);
    server.begin();

    Serial.println("Visualizador SerialScope WiFi Online.");
    
    // Reservar memoria para los buffers UART para evitar fragmentación
    bufferM2S.reserve(512);
    bufferS2M.reserve(512);
}

// ****************************************************************************************
//                                    MAIN LOOP          
// ****************************************************************************************

void loop() {

    // Limpieza de clientes WebSocket desconectados
    ws.cleanupClients();

    // Procesar comandos desde la cola de comandos
    MensajeComando comandoRecibido;
    // Saca el elemento más antiguo de la cola. 
    if (xQueueReceive(colaComandos, &comandoRecibido, 0) == pdTRUE) {
        String cmd = String(comandoRecibido.cmd);
        ejecutarComando(cmd);
    }
    
    if (estaConfigurando) return;

    // Lógica del LED Blanco: Parpadea si no hay clientes, fijo si hay conexión.
    if (ws.count() == 0) {
        if (millis() - lastBlinkTime > 1000) {
            blinkState = !blinkState;
            digitalWrite(LED_WIFI_BLANCO, blinkState ? HIGH : LOW);
            lastBlinkTime = millis();
        }
    } else {
        // Hay clientes conectados: LED Blanco Fijo
        digitalWrite(LED_WIFI_BLANCO, HIGH);
    }

    // Apagar temporalmente el LED verde tras un PONG
    if (greenLedTurnOffTime > 0 && millis() > greenLedTurnOffTime) {
        digitalWrite(LED_STATUS_VERDE, LOW);
        greenLedTurnOffTime = 0;
    }

    // Lógica de apagado de LEDs de estado (Eliminados flickers de actividad)
    if (greenLedTurnOffTime > 0 && millis() > greenLedTurnOffTime) {
        digitalWrite(LED_STATUS_VERDE, LOW);
        greenLedTurnOffTime = 0;
    }

    // --- LÓGICA DE PARPADEO FRECUENCIA SPI (PIN 4) ---
    if (freqBlinkTarget > 0) {
        unsigned long currentMillis = millis();
        if (freqResting) {
            if (currentMillis - lastFreqLedTime > 1500) {
                freqResting = false;
                freqBlinkCount = 0;
                lastFreqLedTime = currentMillis;
            }
        } else {
            if (currentMillis - lastFreqLedTime > 150) {
                lastFreqLedTime = currentMillis;
                freqBlinkState = !freqBlinkState;
                digitalWrite(LED_SPI_FREC_AZUL, freqBlinkState ? HIGH : LOW);
                if (!freqBlinkState) {
                    freqBlinkCount++;
                    if (freqBlinkCount >= freqBlinkTarget) {
                        freqResting = true;
                        digitalWrite(LED_SPI_FREC_AZUL, LOW);
                    }
                }
            }
        }
    }

    // Lógica de parpadeo de LEDs 
    if (ledBlinkTarget > 0) {
        int targetLed = LED_UART_ROJO; 
        if (currentProtocol == "I2C") targetLed = LED_I2C_AMARILLO;
        else if (currentProtocol == "SPI") targetLed = LED_SPI_AZUL;

        unsigned long currentMillis = millis();
        if (ledResting) {
            // Descanso del led entre parpadeos
            if (currentMillis - lastProtocolLedTime > 1500) {
                ledResting = false;
                ledBlinkCount = 0;
                lastProtocolLedTime = currentMillis;
            }
        } else {
            // Tiempo que permanece encendido el led por parpadeo
            if (currentMillis - lastProtocolLedTime > 200) {
                lastProtocolLedTime = currentMillis;
                ledBlinkState = !ledBlinkState;
                digitalWrite(targetLed, ledBlinkState ? HIGH : LOW);
                if (!ledBlinkState) {
                    ledBlinkCount++;
                    if (ledBlinkCount >= ledBlinkTarget) {
                        ledResting = true;
                        digitalWrite(targetLed, LOW);
                    }
                }
            }
        }
    }
    
    // --- SNIFFING SPI (Procesamiento de Buffer Circular) ---
    // Si el índice de lectura es distinto al de escritura, significa que hay datos nuevos
    if (spiReadIdx != spiWriteIdx) {
        String data = "SPI_RECV_BUF:";
        while (spiReadIdx != spiWriteIdx) {
            // Enviamos los datos en pares (MOSI, MISO) para que la interfaz los grafique correctamente
            data += String(spiRawBuffer[spiReadIdx], HEX) + ",";
            spiReadIdx = (spiReadIdx + 1) % SPI_BUFFER_SIZE;
            // Limitamos el paquete pero aseguramos que termine en un par (índice par)
            if (data.length() > 150 && (spiReadIdx % 2 == 0)) break; 
        }
        wsSend(data);
    }

    // --- PROXY TRANSPARENTE UART ---
    while (Serial1.available()) {
        uint8_t c = Serial1.read();
        Serial2.write(c); 
        bufferM2S += String(c, HEX) + ",";
        // Seguridad: Si el buffer es muy grande, forzar un envío inmediato
        if (bufferM2S.length() > 500) break;
    }

    while (Serial2.available()) {
        uint8_t c = Serial2.read();
        Serial1.write(c); 
        bufferS2M += String(c, HEX) + ",";
        
        if (bufferS2M.length() > 512) break;  
    }

    // --- SNIFFING I2C (Procesamiento de Buffer) ---
    // Extraemos los datos capturados por las interrupciones
    if (i2cReadIdx != i2cWriteIdx && (millis() - lastI2cTime > 10)) {
        String data = "I2C_RECV_BUF:";
        while (i2cReadIdx != i2cWriteIdx) {
            data += String(i2cRawBuffer[i2cReadIdx], HEX) + ",";
            i2cReadIdx = (i2cReadIdx + 1) % I2C_BUFFER_SIZE;
        }
        wsSend(data);
    }

    // Flush de Buffers UART 
    if (millis() - lastFlushTime > FLUSH_INTERVAL) {
        if (bufferM2S.length() > 0) {
            wsSend("UART_RECV_BUF:M2S:" + bufferM2S);
            bufferM2S = "";
        }
        if (bufferS2M.length() > 0) {
            wsSend("UART_RECV_BUF:S2M:" + bufferS2M);
            bufferS2M = "";
        }
        lastFlushTime = millis();
    }
}


// ****************************************************************************************
//                                    Funciones        
// ****************************************************************************************

// Actualizar LEDs según el protocolo
void actualizarLedsProtocolo(String proto, int blinks) {
    
    // RE-INICIALIZACIÓN: Asegurar que los pines de los LEDs sean SALIDAS.
    // Algunos periféricos (como UART o I2C) pueden resetear el modo de los pines al iniciarse.
    pinMode(LED_UART_ROJO, OUTPUT);
    pinMode(LED_I2C_AMARILLO, OUTPUT);
    pinMode(LED_SPI_AZUL, OUTPUT);
    pinMode(LED_WIFI_BLANCO, OUTPUT);
    pinMode(LED_STATUS_VERDE, OUTPUT);
    pinMode(LED_SPI_FREC_AZUL, OUTPUT);

    // Limpiar el estado de los LEDs de protocolo
    digitalWrite(LED_UART_ROJO, LOW);
    digitalWrite(LED_I2C_AMARILLO, LOW);
    digitalWrite(LED_SPI_AZUL, LOW);
    digitalWrite(LED_STATUS_VERDE, LOW);
    digitalWrite(LED_SPI_FREC_AZUL, LOW);
    
    currentProtocol = proto;
    ledBlinkTarget = blinks;
    ledBlinkCount = 0;
    ledBlinkState = false;
    ledResting = false;
    lastProtocolLedTime = millis();

    // Determinar qué LED prender según el protocolo 
    if (blinks == 0) {
        // Modo Sniffer (Fijo)
        if (proto == "I2C") digitalWrite(LED_I2C_AMARILLO, HIGH); 
        else if (proto == "SPI") digitalWrite(LED_SPI_AZUL, HIGH);   
        else if (proto == "UART") digitalWrite(LED_UART_ROJO, HIGH);
    } 
}

// Enviar texto al WebSocket
void wsSend(String texto) {
    if (ws.count() > 0) {
        ws.textAll(texto);
    }
}

void limpiarTodosLosModos() {
    
    // Detener los 3 protocolos 
    stopSniffingI2C();
    detachInterrupt(digitalPinToInterrupt(18));
    SPI_Sensing.end();
    Serial1.end();
    Serial2.end();
    
    // Limpiar variables de estado
    spiSlaveMode = false;
    i2cStarted = false;
    spiBitCnt = 0;
    i2cBitCount = 0;
    
    // Apagar parpadeo de frecuencia del SPI
    freqBlinkTarget = 0;
    digitalWrite(LED_SPI_FREC_AZUL, LOW);
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

//  Convertir paquete resibido por WS a comando de texto
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("[WiFi] >>> Cliente conectado a WebSockets (ID: %u)\n", client->id());
        client->text("[Sistema] >>> Conectado al Visualizador SerialScope");
        digitalWrite(LED_WIFI_BLANCO, HIGH); // Al conectar: encender LED blanco y mantenerlo fijo
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf(">> [WiFi] Cliente desconectado (ID: %u)\n", client->id());
        actualizarLedsProtocolo("", 0); // Apagar todo cuando se vaya el cliente
    } else if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo*)arg;
        if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    
            //Recibir el string temporalmente
            String msg = "";
            for(size_t i=0; i<len; i++) {
                msg += (char)data[i];
            }
            msg.trim(); 
            
            //Empaquetarlo en la cola
            MensajeComando nuevoComando;
            strncpy(nuevoComando.cmd, msg.c_str(), sizeof(nuevoComando.cmd) - 1);
            nuevoComando.cmd[sizeof(nuevoComando.cmd) - 1] = '\0'; // Asegurar fin de cadena
            
            //Empujar a la cola (sin bloquear el hilo asíncrono)
            if (colaComandos != NULL) {
                xQueueSend(colaComandos, &nuevoComando, (TickType_t)0);
            }
        }
    }
}

//  Interpretación de comandos texto a su funcion   (LIMPIAR PORQUE TIENE FORMATO ANTIGUO)
void ejecutarComando(String cmd) {
    Serial.println("[WS] >>> " + cmd);

    // 1. PING
    if (cmd == "PING") { 
        wsSend("PONG"); 
        digitalWrite(LED_STATUS_VERDE, HIGH);
        greenLedTurnOffTime = millis() + 500; 
        return; 
    }
    
    // 2. CONFIG: Configuración de modo (UART, I2C, SPI)
    if (cmd.startsWith("CONFIG:")) {
        estaConfigurando = true;
        String proto = getParam(cmd, 1);
        String p2 = getParam(cmd, 2);
        String role = "SNIFFER";
        int blinks = 0;

        // --- PARSING INTELIGENTE (Compatible con 3 o 4 partes) ---
        // Si el segundo parámetro empieza con un número, es el formato viejo (ej: CONFIG:RS232:115200)
        bool isOldFormat = (p2.length() > 0 && isDigit(p2.charAt(0)));
        
        if (proto == "UART") {
            long baud = isOldFormat ? p2.toInt() : getParam(cmd, 3).toInt();
            role = isOldFormat ? "PROXY" : p2; // UART es siempre Proxy/Activo
            
            Serial.println("[CONFIG] >>> UART Proxy a " + String(baud) + " (" + role + ")");
            limpiarTodosLosModos();
            
            Serial1.begin(baud, SERIAL_8N1, 16, 17);
            Serial2.begin(baud, SERIAL_8N1, 33, 32);
            
            // Para UART siempre parpadeamos porque es modo Proxy Activo
            blinks = (baud >= 115200) ? 5 : 1;
            actualizarLedsProtocolo("UART", blinks);
            
            // Limpiar parpadeo de frecuencia SPI al cambiar a UART
            freqBlinkTarget = 0;
            digitalWrite(LED_SPI_FREC_AZUL, LOW);

        } else if (proto == "I2C") {
            activeI2CSpeed = isOldFormat ? p2.toInt() : getParam(cmd, 3).toInt();
            role = isOldFormat ? "SNIFFER" : p2;
            
            String addrStr = isOldFormat ? getParam(cmd, 3) : getParam(cmd, 4);
            String regStr = isOldFormat ? getParam(cmd, 4) : getParam(cmd, 5);
            
            if (addrStr != "") activeI2CAddr = strtol(addrStr.c_str(), NULL, 16);
            if (regStr != "") activeI2CReg = strtol(regStr.c_str(), NULL, 16);

            Serial.println("[CONFIG] >>> I2C Speed:" + String(activeI2CSpeed) + " Addr:0x" + String(activeI2CAddr, HEX) + " (" + role + ")");
            
            limpiarTodosLosModos();
            startSniffingI2C(); 
            
            if (role != "SNIFFER") blinks = (activeI2CSpeed == 400000) ? 2 : 1;
            actualizarLedsProtocolo("I2C", blinks);

        } else if (proto == "SPI") {
            activeSPIFreq = isOldFormat ? p2.toInt() : getParam(cmd, 3).toInt();
            role = isOldFormat ? "SNIFFER" : p2;
            activeSPIMode = isOldFormat ? getParam(cmd, 3).toInt() : getParam(cmd, 4).toInt();

            Serial.println("[CONFIG] >>> SPI Freq:" + String(activeSPIFreq) + " Mode:" + String(activeSPIMode) + " (" + role + ")");

            limpiarTodosLosModos();

            pinMode(18, INPUT_PULLUP); // SCK con Pull-Up para evitar ruidos si está suelto
            pinMode(23, INPUT_PULLUP); // MOSI
            pinMode(19, INPUT_PULLUP); // MISO
            pinMode(5, INPUT_PULLUP);  // CS
            
            spiBitCnt = 0; spiReadIdx = 0; spiWriteIdx = 0;
            attachInterrupt(digitalPinToInterrupt(18), onSCKRising, RISING);
            
            if (role != "SNIFFER") blinks = activeSPIMode + 1;
            actualizarLedsProtocolo("SPI", blinks);

            // Configurar parpadeo de frecuencia SPI (No bloqueante)
            if (role != "SNIFFER") {
                freqBlinkTarget = 1; // 1MHz
                if (activeSPIFreq == 4000000) freqBlinkTarget = 2;
                else if (activeSPIFreq == 8000000) freqBlinkTarget = 3;
                else if (activeSPIFreq == 16000000) freqBlinkTarget = 4;
                else if (activeSPIFreq == 20000000) freqBlinkTarget = 5;
                
                freqBlinkCount = 0;
                freqBlinkState = false;
                freqResting = false;
                lastFreqLedTime = millis();
            } else {
                freqBlinkTarget = 0;
                digitalWrite(LED_SPI_FREC_AZUL, LOW);
            }
        }
        
        estaConfigurando = false;
        Serial.println("[SISTEMA] >>> Puertos configurados con éxito");
        wsSend("READY:PUERTOS_OK");
        return;
    }

    // 3. COMANDOS ACTIVOS (Solo si no estamos en CONFIG)
       
    // I2C MASTER SEND
    if (cmd.startsWith("I2C_SEND:")) {
        int f = cmd.indexOf(':');
        int s = cmd.indexOf(':', f+1);
        String addrStr = cmd.substring(f+1, s);
        String dataStr = cmd.substring(s+1);
        int addr = strtol(addrStr.c_str(), NULL, 16);
        
        stopSniffingI2C();
        Wire.begin(21, 22);
        Wire.setClock(activeI2CSpeed); 
    
        Wire.beginTransmission(addr);
        
        int start = 0;
        for (int i = 0; i <= dataStr.length(); i++) {
            if (dataStr.charAt(i) == ',' || i == dataStr.length()) {
                String b = dataStr.substring(start, i);
                if (b.length() > 0) Wire.write((uint8_t)strtol(b.c_str(), NULL, 16));
                start = i + 1;
            }
        }
        uint8_t err = Wire.endTransmission();
        Wire.end();
        startSniffingI2C();
        wsSend(err == 0 ? "I2C_TX_OK" : "I2C_TX_ERR");

        // RE-INICIALIZACIÓN CRÍTICA: El bus I2C puede resetear el modo de los pines cercanos
        actualizarLedsProtocolo(currentProtocol, ledBlinkTarget);
    }

    // SPI SLAVE EMULATION
    if (cmd.startsWith("SPI_SLAVE_ON:")) {
        int f = cmd.indexOf(':');
        String dataStr = cmd.substring(f+1);
        
        Serial.println("[MODO ACTIVO] >>> Configurando Esclavo SPI Emulado...");
        spiSlaveBufferSize = 0;
        int start = 0;
        for (int i = 0; i <= dataStr.length(); i++) {
            if (dataStr.charAt(i) == ',' || i == dataStr.length()) {
                String b = dataStr.substring(start, i);
                if (b.length() > 0 && spiSlaveBufferSize < 256) {
                    spiSlaveBuffer[spiSlaveBufferSize++] = (uint8_t)strtol(b.c_str(), NULL, 16);
                }
                start = i + 1;
            }
        }
        
        pinMode(19, OUTPUT); // MISO como salida para responder
        spiSlaveMode = true;
        spiSlaveBytePtr = 0;
        spiSlaveBitPtr = 0;
        
        Serial.printf("[SISTEMA] >>> Modo Esclavo SPI Activo con %d bytes\n", spiSlaveBufferSize);
        wsSend("READY:PUERTOS_OK");
    }

    // SPI MASTER SEND
    if (cmd.startsWith("SPI_SEND:")) {
        int f = cmd.indexOf(':');
        String dataStr = cmd.substring(f+1);
        
        detachInterrupt(digitalPinToInterrupt(18));
        SPI.begin(18, 19, 23, 5); 
        SPI.beginTransaction(SPISettings(activeSPIFreq, MSBFIRST, spi_modes[activeSPIMode]));
        digitalWrite(5, LOW);
        
        int start = 0;
        for (int i = 0; i <= dataStr.length(); i++) {
            if (dataStr.charAt(i) == ',' || i == dataStr.length()) {
                String b = dataStr.substring(start, i);
                if (b.length() > 0) SPI.transfer((uint8_t)strtol(b.c_str(), NULL, 16));
                start = i + 1;
            }
        }
        digitalWrite(5, HIGH);
        SPI.end();
        pinMode(18, INPUT);
        attachInterrupt(digitalPinToInterrupt(18), onSCKRising, RISING);
        wsSend("SPI_TX_OK");
    }
}


//***************         I2C       **************************** */

// reconfigura los puertos para I2C y activa las interrupciones
void startSniffingI2C() {
    Wire.end();
    pinMode(21, INPUT_PULLUP);
    pinMode(22, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(22), onSCLRising, RISING);
    attachInterrupt(digitalPinToInterrupt(21), onSDAChange, CHANGE);
}

 // ISR-I2C: Se activa cuando cambia el cable de DATOS (SDA)
 void IRAM_ATTR onSDAChange() {
     if (READ_SCL_FAST == HIGH) {
         if (READ_SDA_FAST == LOW) { 
             i2cStarted = true; 
             i2cBitCount = 0; 
             i2cCurrentByte = 0; 
         }
         else { 
             i2cStarted = false; 
         }
     }
 }
 
 // ISR-I2C: Se activa cada vez que el reloj (SCL) sube para construir el byte capturado
 void IRAM_ATTR onSCLRising() {
     if (!i2cStarted) return; 
     
     int bit = READ_SDA_FAST; 
     
     // Construimos el byte: desplazamos lo que tenemos a la izquierda y metemos el nuevo bit
     i2cCurrentByte = (i2cCurrentByte << 1) | bit;
     i2cBitCount++;
     
     // Al completar 8 bits, el byte está listo
     if (i2cBitCount == 8) {
         i2cRawBuffer[i2cWriteIdx] = i2cCurrentByte; // Guardar en el almacén
         i2cWriteIdx = (i2cWriteIdx + 1) % I2C_BUFFER_SIZE; // Mover el índice circular
     } 
     // El 9no bit es el ACK/NACK (confirmación), lo usamos para resetear el conteo
     else if (i2cBitCount == 9) {
         i2cBitCount = 0; 
         i2cCurrentByte = 0;
         lastI2cTime = millis(); // Actualizar tiempo de actividad
     }
 }

 void stopSniffingI2C() {
    detachInterrupt(digitalPinToInterrupt(22));
    detachInterrupt(digitalPinToInterrupt(21));
    Wire.end();
}

//***************         SPI       **************************** */

// ISR-SPI: Se ejecuta cada vez que el reloj SPI (SCK) sube para obtener dato y guarda en cache si forma un byte lo envia al uart.
 void IRAM_ATTR onSCKRising() {
     if (READ_CS_FAST == 0) { 
         if (spiSlaveMode && spiSlaveBufferSize > 0) {
             uint8_t byteActual = spiSlaveBuffer[spiSlaveBytePtr];
             int bitValue = (byteActual >> (7 - spiSlaveBitPtr)) & 1;
             
             if (bitValue) REG_WRITE(GPIO_OUT_W1TS_REG, (1 << 19)); 
             else REG_WRITE(GPIO_OUT_W1TC_REG, (1 << 19));         
             
             spiSlaveBitPtr++;
             if (spiSlaveBitPtr >= 8) {
                 spiSlaveBitPtr = 0;
                 spiSlaveBytePtr = (spiSlaveBytePtr + 1) % spiSlaveBufferSize;
             }
         }

         spiByterx = (spiByterx << 1) | READ_MOSI_FAST;
         spiBytetx = (spiBytetx << 1) | READ_MISO_FAST;
         spiBitCnt++;
         
         if (spiBitCnt == 8) {
             spiRawBuffer[spiWriteIdx] = spiByterx; 
             spiWriteIdx = (spiWriteIdx + 1) % SPI_BUFFER_SIZE;
             spiRawBuffer[spiWriteIdx] = spiBytetx; 
             spiWriteIdx = (spiWriteIdx + 1) % SPI_BUFFER_SIZE;
             
             spiBitCnt = 0;
             spiByterx = 0;
             spiBytetx = 0;
             lastSpiTime = millis();
         }
     } else {
         spiBitCnt = 0; 
         spiByterx = 0;
         spiBytetx = 0;
         if (spiSlaveMode) {
             spiSlaveBytePtr = 0;
             spiSlaveBitPtr = 0;
         }
     }
 }
