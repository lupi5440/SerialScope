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

// Configuracion de WiFi
const char* ssid = "SerialScope_Visualizador";
const char* password = "12345678";  //Minimo debe tener 8 caracteres para contraseña
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Pines para LEDs indicadores 
#define LED_STATUS_VERDE   13  // Verde (Status)
#define LED_WIFI_BLANCO    4   // Blanco (WiFi)
#define LED_UART_ROJO      14  // Rojo (UART)
#define LED_I2C_AMARILLO   27  // Amarillo (I2C)
#define LED_SPI_AZUL       26  // Azul (SPI)
#define LED_SPI_FREC_AZUL  25  // AZUL Frecuencia (SPI)

// --- PINES DE HARDWARE ---
#define UART_LOCAL_RX_PIN  16 // Pin RX nativo
#define UART_LOCAL_TX_PIN  17 // Pin TX nativo
#define UART_PROXY_RX_PIN  33 // Pin RX remapeado
#define UART_PROXY_TX_PIN  32 // Pin TX remapeado
#define I2C_SDA_PIN        21 // Pin Datos I2C
#define I2C_SCL_PIN        22 // Pin Reloj I2C
#define SPI_SCK_PIN        18 // Reloj SPI
#define SPI_MISO_PIN       19 // Master In Slave Out
#define SPI_MOSI_PIN       23 // Master Out Slave In
#define SPI_CS_PIN         5  // Chip Select

// --- TIEMPOS Y PARÁMETROS DEL SISTEMA (Milisegundos) ---
#define WIFI_BLINK_MS          1000  // Parpadeo buscando WiFi
#define PROTOCOL_BLINK_MS      200   // Velocidad parpadeo protocolos
#define PROTOCOL_REST_MS       1500  // Pausa entre secuencias protocolos
#define FREQ_BLINK_MS          150   // Velocidad parpadeo LED Frecuencia
#define FREQ_REST_MS           1500  // Pausa parpadeo LED Frecuencia
#define PING_LED_MS            500   // Duración alerta PING

// Control de parpadeo de los LEDs de protocolo
uint8_t ledBlinkTarget = 0;              // uint8_t para ahorrar memoria
uint8_t ledBlinkCount = 0;
bool ledBlinkState = false;
bool ledResting = false;
unsigned long lastProtocolLedTime = 0;   // Temporizador para el parpadeo

// Control de parpadeo LED frecuencia SPI
uint8_t freqBlinkTarget = 0;            
uint8_t freqBlinkCount = 0;
bool freqBlinkState = false;
bool freqResting = false;
unsigned long lastFreqLedTime = 0;       // Temporizador para la frecuencia

// Control de parpadeo LED de estado y WiFi
bool blinkState = false;                 // Estado del parpadeo general (WiFi)
unsigned long lastBlinkTime = 0;         // Temporizador general (WiFi)
unsigned long whiteLedOffTime = 0; 
unsigned long greenLedTurnOffTime = 0;

// Temporizadores individuales
unsigned long redLedOffTime = 0;
unsigned long blueLedOffTime = 0; 
unsigned long lastFlushTime = 0;       

// Protocolo actual
String currentProtocol = "";

// Instancia para reservar pines SPI 
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
    char cmd[512]; // Soporta comandos de hasta 127 caracteres
} MensajeComando;

// Identificador de la cola de FreeRTOS
QueueHandle_t colaComandos;


// Buffers para UART 
#define UART_BUF_SIZE 256
uint8_t rawBufferM2S[UART_BUF_SIZE];
int idxM2S = 0;

uint8_t rawBufferS2M[UART_BUF_SIZE];
int idxS2M = 0;

// Declaraciones de interrupciones (ISRs para i2c y spi)
void IRAM_ATTR onSCLRising();
void IRAM_ATTR onSDAChange();
void IRAM_ATTR onSCKChange(); 
void IRAM_ATTR onCSChange();  
void onI2CReceive(int numBytes);
void onI2CRequest();

volatile bool i2cSlaveMsgReady = false;
volatile bool i2cSlaveReqReady = false;
char i2cSlaveMsgBuffer[128];
char i2cSlaveReqBuffer[64];

Role i2cRole = ROLE_SNIFFER;
Role spiRole = ROLE_SNIFFER;

 // Parametros de configuracion I2C
long activeI2CSpeed = 100000;
int activeI2CAddr = 0x08;
int activeI2CReg = 0x00;

 // Varibles de snifing I2C
 #define I2C_BUFFER_SIZE 1024
 volatile uint8_t i2cRawBuffer[I2C_BUFFER_SIZE]; // Almacén circular para los bytes capturados
 volatile int i2cWriteIdx = 0;                   // Dónde escribe el sniffer (interrupciones)
 volatile int i2cReadIdx = 0;                    // Dónde lee el programa principal (loop)
 volatile unsigned long lastI2cTime = 0;         // Marca de tiempo para detectar fin de ráfaga
 volatile uint8_t i2cCurrentByte = 0;            // Byte que se está construyendo bit a bit
 volatile bool i2cStarted = false;               // Para pausar la captura de datos 
volatile int i2cBitCount = 0;                    // Contador de bits para identificar si es dirección o data

// Definiciones de lectura directa para I2C
#define READ_SCL_FAST ((REG_READ(GPIO_IN_REG) >> 22) & 1)
#define READ_SDA_FAST ((REG_READ(GPIO_IN_REG) >> 21) & 1)
 
// Variables de configuracion modo esclavo I2C y SPI
volatile uint8_t sensorMemory[256]; // 256 registros virtuales para emular el sensor
volatile uint8_t currentRegister = 0; // Apuntador al registro actual I2C

volatile bool spiSlaveMode = false;
volatile int spiSlaveBytePtr = 0;  //indice inicio registro byte
volatile int spiSlaveBitPtr = 0;   //indice bit
volatile uint8_t spiSlaveBuffer[256]; //buffer para SPI
volatile int spiSlaveBufferSize = 0;

// Parametros de configuracion SPI
long activeSPIFreq = 1000000;
int activeSPIMode = 0;

// Variables de estado SPI
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
 #define READ_SCK_FAST  ((REG_READ(GPIO_IN_REG) >> 18) & 1)  //Saber si esta activo o no el SCK

// Setup principal
void setup() {
    
    // Inicialización de comunicación serial
    Serial.begin(115200);
    
    // DESACTIVAR BROWNOUT DETECTOR (Para evitar reinicios por caídas leves de voltaje)
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
    
    delay(2000); // Esperar a que el voltaje se estabilice tras el encendido

    // Configuración de Pines Seriales 
    Serial1.begin(115200, SERIAL_8N1, UART_LOCAL_RX_PIN, UART_LOCAL_TX_PIN); 
    Serial2.begin(115200, SERIAL_8N1, UART_PROXY_RX_PIN, UART_PROXY_TX_PIN);
    
    // Cola de comando para evitar condicion de carrera entre loop y web socket
    colaComandos = xQueueCreate(10, sizeof(MensajeComando));
    if(colaComandos == NULL){
        Serial.println("[Error] >>> No se pudo crear la cola de comandos");
    }

    // Configuración de Pines SPI 
    SPI_Sensing.begin(18, 19, 23, 5);

    // Configurar interrupciones I2C 
    pinMode(I2C_SDA_PIN, INPUT_PULLUP);
    pinMode(I2C_SCL_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(I2C_SCL_PIN), onSCLRising, RISING);
    attachInterrupt(digitalPinToInterrupt(I2C_SDA_PIN), onSDAChange, CHANGE);

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

    //btener el mejor canal de forma dinámica
    int canalOptimo = obtenerMejorCanalWiFi();
    
    // Configura el punto de acceso Wi-Fi
    WiFi.mode(WIFI_AP); 
    WiFi.softAP(ssid, password, canalOptimo, 0, 4);
    digitalWrite(LED_WIFI_BLANCO, HIGH);
    
    // Inicia el servidor web para manejar la comunicación con la interfaz web
    ws.onEvent(onEvent);
    server.addHandler(&ws);
    server.begin();

    Serial.println("[Sistema] >>> Visualizador SerialScope WiFi Online.");
    
}

// Main loop
void loop() {

    // Limpieza de clientes WebSocket desconectados
    ws.cleanupClients();

    // Procesar comandos desde la cola de comandos
    MensajeComando comandoRecibido;
    if (xQueueReceive(colaComandos, &comandoRecibido, 0) == pdTRUE) {
        String cmd = String(comandoRecibido.cmd);
        ejecutarComando(cmd);
    }
    
    if (estaConfigurando) return;

    manejarLEDs();
    
    // Lógica de lectura para UART como proxy transparente
    if (currentProtocol == "UART") {
        int readLimit = 0; // Freno de seguridad
        
        while (Serial1.available() && idxM2S < UART_BUF_SIZE && readLimit < 64) {
            uint8_t c = Serial1.read();
            Serial2.write(c); 
            rawBufferM2S[idxM2S++] = c;
            readLimit++;
        }

        readLimit = 0;
        while (Serial2.available() && idxS2M < UART_BUF_SIZE && readLimit < 64) {
            uint8_t c = Serial2.read();
            Serial1.write(c); 
            rawBufferS2M[idxS2M++] = c;
            readLimit++;
        }
    }

    // Empaquetado y envio por WiFi para SPI, I2C y UART
    if (millis() - lastFlushTime > FLUSH_INTERVAL) {
        
        if (currentProtocol == "SPI") {
            if (spiReadIdx != spiWriteIdx) {
                lastSpiTime = millis();  
                
                // Buffer estático para SPI
                char sendBuf[512]; 
                int len = snprintf(sendBuf, sizeof(sendBuf), "SPI_RECV_BUF:");
                int maxBytes = 0;
                
                // Escribimos directamente en el buffer estático
                while (spiReadIdx != spiWriteIdx && maxBytes < 150 && len < 490) { 
                    len += snprintf(sendBuf + len, sizeof(sendBuf) - len, "%02X,", spiRawBuffer[spiReadIdx]); 
                    spiReadIdx = (spiReadIdx + 1) % SPI_BUFFER_SIZE;
                    maxBytes++;
                }
                wsSend(String(sendBuf));
            }
        }
        
        else if (currentProtocol == "I2C") {
            if (i2cReadIdx != i2cWriteIdx && (millis() - lastI2cTime > 10)) {
                lastI2cTime = millis();  
                
                char sendBuf[512]; 
                int len = snprintf(sendBuf, sizeof(sendBuf), "I2C_RECV_BUF:");
                int maxBytes = 0;
                
                while (i2cReadIdx != i2cWriteIdx && maxBytes < 150 && len < 490) { 
                    len += snprintf(sendBuf + len, sizeof(sendBuf) - len, "%02X,", i2cRawBuffer[i2cReadIdx]);
                    i2cReadIdx = (i2cReadIdx + 1) % I2C_BUFFER_SIZE;
                    maxBytes++;
                }
                wsSend(String(sendBuf));
            }
        }
        
        else if (currentProtocol == "UART") {
            if (idxM2S > 0) {
                char sendBuf[512];
                int len = snprintf(sendBuf, sizeof(sendBuf), "UART_RECV_BUF:M2S:");
                for (int i = 0; i < idxM2S && len < 490; i++) {
                    len += snprintf(sendBuf + len, sizeof(sendBuf) - len, "%02X,", rawBufferM2S[i]); 
                }
                wsSend(String(sendBuf));
                idxM2S = 0; 
            }

            if (idxS2M > 0) {
                char sendBuf[512];
                int len = snprintf(sendBuf, sizeof(sendBuf), "UART_RECV_BUF:S2M:");
                for (int i = 0; i < idxS2M && len < 490; i++) {
                    len += snprintf(sendBuf + len, sizeof(sendBuf) - len, "%02X,", rawBufferS2M[i]);
                }
                wsSend(String(sendBuf));
                idxS2M = 0; 
            }
        }
        
        lastFlushTime = millis(); 
    }

    // Revisamos las banderas del Esclavo I2C para enviar mensaje
    if (i2cSlaveMsgReady) {
        wsSend(String((char*)i2cSlaveMsgBuffer));
        i2cSlaveMsgReady = false;
    }
    if (i2cSlaveReqReady) {
        wsSend(String((char*)i2cSlaveReqBuffer));
        i2cSlaveReqReady = false;
    }
}

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

// Enviar texto al WebSocket (protege contra saturación)
void wsSend(String texto) {
    if (ws.count() > 0) {
        ws.textAll(texto);
    }
}

void limpiarTodosLosModos() {
    
    // Detener los 3 protocolos 
    stopSniffingI2C();
    detachInterrupt(digitalPinToInterrupt(SPI_SCK_PIN));
    detachInterrupt(digitalPinToInterrupt(SPI_CS_PIN));
    SPI_Sensing.end();
    Serial1.end();
    Serial2.end();
    
    // Limpiar variables de estado
    spiSlaveMode = false;
    i2cStarted = false;
    spiBitCnt = 0;
    i2cBitCount = 0;
    
    // Limpiar el estado de los LEDs de protocolo
    digitalWrite(LED_UART_ROJO, LOW);
    digitalWrite(LED_I2C_AMARILLO, LOW);
    digitalWrite(LED_SPI_AZUL, LOW);
    digitalWrite(LED_STATUS_VERDE, LOW);
    digitalWrite(LED_SPI_FREC_AZUL, LOW);
    
    // Apagar parpadeo de frecuencia del SPI
    freqBlinkTarget = 0;
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

// Convertir paquete recibido por WS a comando de texto
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
            msg.reserve(len + 1); // Reserva de memoria para evitar fragmentación
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

    // 1. Ping de conexión y medición de latencia
    if (cmd.startsWith("PING")) {
        String timestamp = getParam(cmd, 1);
        if (timestamp != "") {
            wsSend("PONG:" + timestamp); 
        } else {
            wsSend("PONG"); 
        }
        
        digitalWrite(LED_STATUS_VERDE, HIGH);
        greenLedTurnOffTime = millis() + 500; 
        return; 
    }
    
    // 2. CONFIG: Configuración de modos sniffing (UART, I2C, SPI)
    if (cmd.startsWith("CONFIG:")) {
        estaConfigurando = true;
        
        // 1. Extraemos protocolo y rol
        String proto = getParam(cmd, 1);
        String role  = getParam(cmd, 2); //SNIFFER, MASTER, SLAVE, etc.

        // Limpiamos espacios en blanco o saltos de línea basura que el WebSocket pudiera añadir
        proto.trim();
        role.trim();

        int blinks = 0;

        if (proto == "UART") {
            // JS: CONFIG:UART:ROL:BAUD
            long baud = getParam(cmd, 3).toInt();
            
            Serial.println("[CONFIG] >>> UART " + role + " a " + String(baud) + " bps");
            limpiarTodosLosModos();
            
            Serial1.begin(baud, SERIAL_8N1, UART_LOCAL_RX_PIN, UART_LOCAL_TX_PIN); 
            Serial2.begin(baud, SERIAL_8N1, UART_PROXY_RX_PIN, UART_PROXY_TX_PIN);
            
            blinks = (baud >= 115200) ? 5 : 1;
            actualizarLedsProtocolo("UART", blinks);
            
            freqBlinkTarget = 0;
            digitalWrite(LED_SPI_FREC_AZUL, LOW);

        }   else if (proto == "I2C") {
            // JS: CONFIG:I2C:ROL:SPEED:ADDR:REG
            activeI2CSpeed = getParam(cmd, 3).toInt();
            String addrStr = getParam(cmd, 4);
            if (addrStr != "") activeI2CAddr = strtol(addrStr.c_str(), NULL, 16);

            Serial.println("[CONFIG] >>> I2C Speed:" + String(activeI2CSpeed) + " Addr:0x" + String(activeI2CAddr, HEX) + " (" + role + ")");
            
            limpiarTodosLosModos();
            
            if (role == "SNIFFER" || role == "MASTER") {
                startSniffingI2C(); 
            } else if (role == "SLAVE") {
                // INICIAMOS COMO SENSOR ESCLAVO FALSO
                Wire.begin((uint8_t)activeI2CAddr);
                Wire.onReceive(onI2CReceive);
                Wire.onRequest(onI2CRequest);
            }
            
            if (role != "SNIFFER") blinks = (activeI2CSpeed == 400000) ? 2 : 1;
            actualizarLedsProtocolo("I2C", blinks);

        } else if (proto == "SPI") {
            // JS: CONFIG:SPI:ROL:FREQ:MODE
            activeSPIFreq = getParam(cmd, 3).toInt();
            activeSPIMode = getParam(cmd, 4).toInt();

            Serial.println("[CONFIG] >>> SPI Freq:" + String(activeSPIFreq) + " Mode:" + String(activeSPIMode) + " (" + role + ")");

            limpiarTodosLosModos();

            pinMode(SPI_SCK_PIN, INPUT_PULLUP); // SCK
            pinMode(SPI_MOSI_PIN, INPUT_PULLUP); // MOSI
            pinMode(SPI_MISO_PIN, INPUT_PULLUP); // MISO
            pinMode(SPI_CS_PIN, INPUT_PULLUP);  // CS
            
            spiBitCnt = 0; spiReadIdx = 0; spiWriteIdx = 0;

            attachInterrupt(digitalPinToInterrupt(SPI_SCK_PIN), onSCKChange, CHANGE);
            attachInterrupt(digitalPinToInterrupt(SPI_CS_PIN), onCSChange, CHANGE);

            if (role != "SNIFFER") blinks = activeSPIMode + 1;
            actualizarLedsProtocolo("SPI", blinks);

            // Configurar parpadeo de frecuencia SPI (SOLO SI ES MAESTRO)
            if (role == "MASTER") {
                freqBlinkTarget = 1; 
                if (activeSPIFreq >= 1000000) freqBlinkTarget = 2;
                if (activeSPIFreq >= 4000000) freqBlinkTarget = 3;
                if (activeSPIFreq >= 8000000) freqBlinkTarget = 4;
                if (activeSPIFreq >= 10000000) freqBlinkTarget = 5;
                if (activeSPIFreq >= 20000000) freqBlinkTarget = 6;
                
                freqBlinkCount = 0;
                freqBlinkState = false;
                freqResting = false;
                lastFreqLedTime = millis();
            } else {
                // En Esclavo o Sniffer, el LED azul de frecuencia se apaga
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
        if (err == 0) {
            wsSend("I2C_TX_OK");
            digitalWrite(LED_STATUS_VERDE, LOW);
        } else {
            wsSend("I2C_TX_ERR");
            digitalWrite(LED_STATUS_VERDE, HIGH); // Alerta de error en bus
        }

        // RE-INICIALIZACIÓN CRÍTICA: El bus I2C puede resetear el modo de los pines cercanos
        pinMode(LED_I2C_AMARILLO, OUTPUT);
        return;
    }

    // I2C: MAESTRO ESCRIBE
    if (cmd.startsWith("I2C_WRITE:")) {
        String addrStr = getParam(cmd, 1);
        String regStr = getParam(cmd, 2);
        String dataStr = getParam(cmd, 3);
        
        int addr = strtol(addrStr.c_str(), NULL, 16);
        int reg = strtol(regStr.c_str(), NULL, 16);
        
        stopSniffingI2C();
        Wire.begin(21, 22);
        Wire.setClock(activeI2CSpeed); 
    
        Wire.beginTransmission(addr);
        Wire.write(reg); 
        
        int start = 0;
        for (int i = 0; i <= dataStr.length(); i++) {
            if (dataStr.charAt(i) == ',' || i == dataStr.length()) {
                String b = dataStr.substring(start, i);
                if (b.length() > 0) Wire.write((uint8_t)strtol(b.c_str(), NULL, 16));
                start = i + 1;
            }
        }
        uint8_t err = Wire.endTransmission();
        startSniffingI2C(); 
        
        if (err == 0) {
            wsSend("I2C_RECV_BUF:" + addrStr + "," + regStr + "," + dataStr);
            digitalWrite(LED_STATUS_VERDE, LOW);
        } else {
            wsSend("I2C_TX_ERR:Sin respuesta");
            digitalWrite(LED_STATUS_VERDE, HIGH); // Alerta de error en bus
        }
        
        pinMode(LED_I2C_AMARILLO, OUTPUT);
        return;
    }

    // I2C: MAESTRO LEE 
    if (cmd.startsWith("I2C_READ:")) {
        String addrStr = getParam(cmd, 1);
        String regStr = getParam(cmd, 2);
        int numBytes = getParam(cmd, 3).toInt();
        
        int addr = strtol(addrStr.c_str(), NULL, 16);
        int reg = strtol(regStr.c_str(), NULL, 16);
        
        stopSniffingI2C();
        Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
        Wire.setClock(activeI2CSpeed); 
        
        Wire.beginTransmission(addr);
        Wire.write(reg);
        uint8_t err = Wire.endTransmission(false); 
        
        if (err != 0) {
            startSniffingI2C();
            wsSend("I2C_TX_ERR:NACK en lectura");
            digitalWrite(LED_STATUS_VERDE, HIGH);
            return;
        }        
        Wire.requestFrom((uint16_t)addr, (uint8_t)numBytes, true);
        
        // Buffer estático para I2C
        char readBuffer[256]; 
        int len = 0;
        
        while(Wire.available() && len < 240) { 
            len += snprintf(readBuffer + len, sizeof(readBuffer) - len, "%02X,", Wire.read());
        }
        
        startSniffingI2C();
        
        wsSend("I2C_RECV_BUF:" + addrStr + "," + regStr + "," + String(readBuffer));
        wsSend("I2C_READ_RES:" + String(readBuffer));
        pinMode(LED_I2C_AMARILLO, OUTPUT);
        return;
    }

    // Llenar memoria del sensor virtual (I2C)
    if (cmd.startsWith("SET_MEMORY:")) {
        int reg = strtol(getParam(cmd, 1).c_str(), NULL, 16);
        String dataStr = getParam(cmd, 2);
        
        int start = 0;
        int ptr = reg;
        for (int i = 0; i <= dataStr.length(); i++) {
            if (dataStr.charAt(i) == ',' || i == dataStr.length()) {
                String b = dataStr.substring(start, i);
                if (b.length() > 0 && ptr < 256) {
                    sensorMemory[ptr++] = (uint8_t)strtol(b.c_str(), NULL, 16);
                }
                start = i + 1;
            }
        }
        wsSend("READY:PUERTOS_OK");
        return;
    }

    // SPI: ESCLAVO EMULADO
    if (cmd.startsWith("SPI_SLAVE_ON:")) {
        String dataStr = getParam(cmd, 1);
        
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
        
        pinMode(SPI_MISO_PIN, OUTPUT); 
        spiSlaveMode = true;
        spiSlaveBytePtr = 0;
        spiSlaveBitPtr = 0;
        
        Serial.printf("[SISTEMA] >>> Modo Esclavo SPI Activo con %d bytes\n", spiSlaveBufferSize);
        wsSend("READY:PUERTOS_OK");
        return; 
    }

    // SPI: MAESTRO SEND (Solo enviar, sin importar lo que reciba)
    if (cmd.startsWith("SPI_SEND:")) {
        int f = cmd.indexOf(':');
        String dataStr = cmd.substring(f+1);
        
        // 1. Pausamos TODO el sniffer (SCK y CS)
        detachInterrupt(digitalPinToInterrupt(SPI_SCK_PIN));
        detachInterrupt(digitalPinToInterrupt(SPI_CS_PIN));
        
        // 2. Usamos SPI_Sensing y liberamos CS (-1)
        SPI_Sensing.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, -1); 
        SPI_Sensing.beginTransaction(SPISettings(activeSPIFreq, MSBFIRST, spi_modes[activeSPIMode]));
        
        // 3. Tomamos control manual del CS
        pinMode(SPI_CS_PIN, OUTPUT);
        digitalWrite(SPI_CS_PIN, LOW);
        
        int start = 0;
        for (int i = 0; i <= dataStr.length(); i++) {
            if (dataStr.charAt(i) == ',' || i == dataStr.length()) {
                String b = dataStr.substring(start, i);
                if (b.length() > 0) {
                    SPI_Sensing.transfer((uint8_t)strtol(b.c_str(), NULL, 16));
                }
                start = i + 1;
            }
        }
        
        // 4. Terminamos transferencia cerrando todo en orden
        digitalWrite(SPI_CS_PIN, HIGH);
        SPI_Sensing.endTransaction(); // Libera el RTOS
        SPI_Sensing.end();            // Apaga el hardware
        
        // 5. Regresamos los pines a modo lectura segura para el sniffer
        pinMode(SPI_SCK_PIN, INPUT_PULLUP); 
        pinMode(SPI_CS_PIN, INPUT_PULLUP);
        
        // Reactivamos interrupciones
        attachInterrupt(digitalPinToInterrupt(SPI_SCK_PIN), onSCKChange, CHANGE);
        attachInterrupt(digitalPinToInterrupt(SPI_CS_PIN), onCSChange, CHANGE);
        
        wsSend("SPI_TX_OK");
        return;
    }

    // SPI: MAESTRO TRANSFERENCIA 
    if (cmd.startsWith("SPI_TRANSFER:")) {
        String dataStr = getParam(cmd, 1);
        
        // Pausamos el sniffer temporalmente
        detachInterrupt(digitalPinToInterrupt(SPI_SCK_PIN));
        detachInterrupt(digitalPinToInterrupt(SPI_CS_PIN));
        
        // 1. Usamos el objeto SPI_Sensing (no el global) y dejamos CS (-1) libre
        SPI_Sensing.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, -1); 
        SPI_Sensing.beginTransaction(SPISettings(activeSPIFreq, MSBFIRST, spi_modes[activeSPIMode]));
        
        // 2. Configuramos el CS explícitamente como SALIDA
        pinMode(SPI_CS_PIN, OUTPUT);
        digitalWrite(SPI_CS_PIN, LOW);
        
        String bufferGraph = "";
        char rxHex[6]; // Margen extra de seguridad
        char txHex[6];
        
        int start = 0;
        for (int i = 0; i <= dataStr.length(); i++) {
            if (dataStr.charAt(i) == ',' || i == dataStr.length()) {
                String b = dataStr.substring(start, i);
                if (b.length() > 0) {
                    uint8_t txByte = (uint8_t)strtol(b.c_str(), NULL, 16);
                    uint8_t rxByte = SPI_Sensing.transfer(txByte); // Usamos SPI_Sensing
                    
                    // snprintf es más seguro contra desbordamientos que sprintf
                    snprintf(txHex, sizeof(txHex), "%02X,", txByte);
                    snprintf(rxHex, sizeof(rxHex), "%02X,", rxByte);
                    bufferGraph += String(txHex) + String(rxHex);
                }
                start = i + 1;
            }
        }
        
        // 3. Terminamos transferencia en el orden CORRECTO
        digitalWrite(SPI_CS_PIN, HIGH);
        SPI_Sensing.endTransaction(); // ¡VITAL! Libera el candado del RTOS
        SPI_Sensing.end();            // Apaga el hardware
        
        // 4. Regresamos los pines a modo lectura para protegerlos
        pinMode(SPI_SCK_PIN, INPUT_PULLUP);
        pinMode(SPI_CS_PIN, INPUT_PULLUP);
        
        // Reactivamos las interrupciones del sniffer
        attachInterrupt(digitalPinToInterrupt(SPI_SCK_PIN), onSCKChange, CHANGE);
        attachInterrupt(digitalPinToInterrupt(SPI_CS_PIN), onCSChange, CHANGE);
        
        wsSend("SPI_RECV_BUF:" + bufferGraph);
        pinMode(LED_SPI_AZUL, OUTPUT);
        return;
    }
}

// reconfigura los puertos para I2C y activa las interrupciones
void startSniffingI2C() {
    Wire.end();
    pinMode(I2C_SDA_PIN, INPUT_PULLUP);
    pinMode(I2C_SCL_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(I2C_SCL_PIN), onSCLRising, RISING);
    attachInterrupt(digitalPinToInterrupt(I2C_SDA_PIN), onSDAChange, CHANGE);
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
     }
 }

 void stopSniffingI2C() {
    detachInterrupt(digitalPinToInterrupt(I2C_SCL_PIN)); 
    detachInterrupt(digitalPinToInterrupt(I2C_SDA_PIN)); 
    Wire.end();
}

// ISR-CS: Manejo del Chip Select con soporte dinámico para los 4 modos
void IRAM_ATTR onCSChange() {
    if (READ_CS_FAST == 0) {
        // --- INICIA TRANSMISIÓN (CS BAJA) ---
        
        // 1. Tomamos control del pin MISO (Lo convertimos en Salida) dinámicamente
        if (spiSlaveMode) {
            REG_WRITE(GPIO_ENABLE_W1TS_REG, (1 << 19)); 
        }

        // Modos 0 y 2 (CPHA = 0) necesitan precargar el primer bit ANTES del reloj
        if ((activeSPIMode == 0 || activeSPIMode == 2) && spiSlaveMode && spiSlaveBufferSize > 0) {
            uint8_t byteActual = spiSlaveBuffer[spiSlaveBytePtr];
            int bitValue = (byteActual >> 7) & 1; // Leemos el bit más significativo (bit 7)
            
            if (bitValue) REG_WRITE(GPIO_OUT_W1TS_REG, (1 << 19)); // MISO HIGH
            else REG_WRITE(GPIO_OUT_W1TC_REG, (1 << 19));          // MISO LOW
            
            spiSlaveBitPtr = 1; // Ya cargamos el bit 0, el reloj empezará en el bit 1
        }
    } else {
        // Termina transmisión (CS sube)
        // 1. Liberamos el pin MISO (Lo regresamos a Entrada / High-Z) para no bloquear el bus
        if (spiSlaveMode) {
            REG_WRITE(GPIO_ENABLE_W1TC_REG, (1 << 19)); 
        }

        // Reseteamos contadores para el siguiente paquete
        spiBitCnt = 0; 
        spiByterx = 0;
        spiBytetx = 0;
        if (spiSlaveMode) {
            spiSlaveBytePtr = 0;
            spiSlaveBitPtr = 0;
        }
    }
}

// ISR-SPI: Manejo del reloj soportando los 4 modos dinámicamente
void IRAM_ATTR onSCKChange() {
    // Si el Chip Select (CS) está inactivo (1), ignoramos el reloj (evita ruido)
    if (READ_CS_FAST == 1) return;

    uint8_t sckState = READ_SCK_FAST;

    // MAGIA DE LOS 4 MODOS:
    // Los Modos 0 y 3 muestrean (leen) cuando el reloj está en ALTO (1)
    // Los Modos 1 y 2 muestrean (leen) cuando el reloj está en BAJO (0)
    bool isSampleEdge = (activeSPIMode == 0 || activeSPIMode == 3) ? (sckState == 1) : (sckState == 0);

    if (isSampleEdge) {
        // --- FLANCO DE LECTURA (MUESTREO) ---
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
        }
    } 
    else {
        // Flanco de escritura (SHIFT)
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
    }
}

// ISR-I2C Hardware: Se ejecuta cuando el Maestro I2C nos ESCRIBE
void onI2CReceive(int numBytes) {
    if (Wire.available()) {
        uint8_t reg = Wire.read();
        currentRegister = reg;
        
        // Guardamos rápido en el buffer de texto en lugar de usar Strings pesados
        int len = snprintf((char*)i2cSlaveMsgBuffer, sizeof(i2cSlaveMsgBuffer), "I2C_RECV_BUF:M2S:%02X,%02X", activeI2CAddr, reg);
        
        while(Wire.available() && len < 120) {
            uint8_t data = Wire.read();
            sensorMemory[currentRegister] = data;
            len += snprintf((char*)i2cSlaveMsgBuffer + len, sizeof(i2cSlaveMsgBuffer) - len, ",%02X", data);
            currentRegister++;
        }
        i2cSlaveMsgReady = true; // Levantamos la bandera para el loop
    }
}

// ISR-I2C Hardware: Se ejecuta cuando el Maestro I2C nos LEE
void onI2CRequest() {
    uint8_t data = sensorMemory[currentRegister];
    Wire.write(data); 
    
    // Guardamos rápido en el buffer
    snprintf((char*)i2cSlaveReqBuffer, sizeof(i2cSlaveReqBuffer), "I2C_RECV_BUF:S2M:%02X,%02X", activeI2CAddr, data);
    i2cSlaveReqReady = true; // Levantamos la bandera para el loop
    
    currentRegister++;
}

// Función para encontrar el mejor canal Wi-Fi (1-13)
int obtenerMejorCanalWiFi() {
    Serial.println("[WiFi] >>> Escaneando espectro para buscar el canal más limpio...");
    
    // Ponemos el WiFi en modo estación temporalmente para escanear
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    int n = WiFi.scanNetworks();
    if (n == 0) {
        Serial.println("[WiFi] >>> No se encontraron otras redes. Usando Canal 1.");
        return 1; // Si no hay nadie, el 1 es ideal
    }

    // Cuántas redes hay en cada canal (1 al 13)
    int usoDeCanales[14] = {0}; 
    
    for (int i = 0; i < n; ++i) {
        int canal = WiFi.channel(i);
        if(canal >= 1 && canal <= 13) {
            usoDeCanales[canal]++;
        }
    }

    // Buscar el canal con el menor número de redes
    // Priorizamos los canales que no se solapan en 2.4GHz: 1, 6 y 11
    int mejorCanal = 1;
    int minRedes = usoDeCanales[1];

    int canalesPreferidos[] = {1, 6, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13};
    
    for (int i = 0; i < 13; i++) {
        int c = canalesPreferidos[i];
        if (usoDeCanales[c] < minRedes) {
            minRedes = usoDeCanales[c];
            mejorCanal = c;
        }
    }

    Serial.printf("[WiFi] >>> Escaneo completo. Mejor canal encontrado: %d (Redes detectadas ahí: %d)\n", mejorCanal, minRedes);
    
    // Liberar memoria del escaneo
    WiFi.scanDelete();
    return mejorCanal;
}

void manejarLEDs() {
    // 1. Lógica del LED Blanco (WiFi)
    if (ws.count() == 0) {
        if (millis() - lastBlinkTime > WIFI_BLINK_MS) {
            blinkState = !blinkState;
            digitalWrite(LED_WIFI_BLANCO, blinkState ? HIGH : LOW);
            lastBlinkTime = millis();
        }
    } else {
        digitalWrite(LED_WIFI_BLANCO, HIGH);
    }

    // 2. Apagar temporalmente el LED verde tras un PONG
    if (greenLedTurnOffTime > 0 && millis() > greenLedTurnOffTime) {
        digitalWrite(LED_STATUS_VERDE, LOW);
        greenLedTurnOffTime = 0;
    }

    // 3. Lógica de parpadeo de LED de frecuencia (PIN 4) 
    if (freqBlinkTarget > 0) {
        unsigned long currentMillis = millis();
        if (freqResting) {
            if (currentMillis - lastFreqLedTime > FREQ_REST_MS) {
                freqResting = false;
                freqBlinkCount = 0;
                lastFreqLedTime = currentMillis;
            }
        } else {
            if (currentMillis - lastFreqLedTime > FREQ_BLINK_MS) {
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

    // 4. Lógica de parpadeo de LEDs de protocolos
    if (ledBlinkTarget > 0) {
        int targetLed = LED_UART_ROJO; 
        if (currentProtocol == "I2C") targetLed = LED_I2C_AMARILLO;
        else if (currentProtocol == "SPI") targetLed = LED_SPI_AZUL;

        unsigned long currentMillis = millis();
        if (ledResting) {
            if (currentMillis - lastProtocolLedTime > PROTOCOL_REST_MS) {
                ledResting = false;
                ledBlinkCount = 0;
                lastProtocolLedTime = currentMillis;
            }
        } else {
            if (currentMillis - lastProtocolLedTime > PROTOCOL_BLINK_MS) {
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
}