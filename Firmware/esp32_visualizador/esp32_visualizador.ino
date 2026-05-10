/*
 * =====================================================================================
 *  PROYECTO : SerialScope — Visualizador (Sniffer / Proxy)
 *  ARCHIVO  : esp32_visualizador.ino
 *  AUTOR    : SerialScope Team
 * =====================================================================================
 *
 *  DESCRIPCIÓN:
 *    Firmware del ESP32 Visualizador. Captura tráfico de bus y lo retransmite a la interfaz web vía WebSocket sobre WiFi.
 *
 *  MODOS DE OPERACIÓN:
 *    - UART / RS232 : Proxy transparente bidireccional.
 *                    Todo lo que llega de un lado se reenvía al otro y se reporta a la web.
 *    - I²C         : Sniffing pasivo mediante interrupciones en SCL/SDA.
 *                    Captura bytes a nivel de bit sin interferir en el bus.
 *    - SPI          : Sniffing pasivo mediante interrupciones en SCK y CS.
 *
 *  PINOUT:
 *    LEDs     → GPIO 14 (Rojo/UART), 15 (Azul/I2C), 13 (Amarillo/SPI)
 *               GPIO 12 (Blanco/WiFi),  27 (Verde/Error)
 *    UART     → Serial1: RX:25, TX:26  |  Serial2: RX:33, TX:32
 *    I²C      → SCL:22, SDA:4 
 *    SPI      → SCK:18, MISO:19, MOSI:23, CS:5
 *
 *  RED WiFi: "SerialScope_Visualizador" (abierta)
 *  WebSocket: ws://192.168.4.1
 *
 * =====================================================================================
 */

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Wire.h>
#include <SPI.h>

const char* ssid = "SerialScope_Visualizador";
const char* password = ""; // Red Abierta

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Pines para LEDs indicadores
#define LED_MODO_ROJO 14
#define LED_MODO_AZUL 27
#define LED_MODO_AMARILLO 13  
#define LED_WIFI_BLANCO 2    
#define LED_ERROR_VERDE 15

String currentProtocol = "";
int ledBlinkTarget = 0;
int ledBlinkCount = 0;
bool ledBlinkState = false;
unsigned long lastProtocolLedTime = 0;
bool ledResting = false;

void actualizarLedsProtocolo(String proto, int blinks) {
    // Apagar todos los LEDs de protocolo primero
    digitalWrite(LED_MODO_ROJO, LOW);
    digitalWrite(LED_MODO_AZUL, LOW);
    digitalWrite(LED_MODO_AMARILLO, LOW);
    
    currentProtocol = proto;

    if (proto == "I2C") {
        // I2C: LED azul fijo encendido (sniffing pasivo)
        digitalWrite(LED_MODO_AZUL, HIGH);
        ledBlinkTarget = 0; // Sin parpadeo
    } else if (proto == "SPI") {
        // SPI: LED amarillo fijo encendido (sniffing pasivo)
        digitalWrite(LED_MODO_AMARILLO, HIGH);
        ledBlinkTarget = 0; // Sin parpadeo
    } else {
        // RS232/UART: LED rojo parpadea según baudrate
        ledBlinkTarget = blinks;
        ledBlinkCount = 0;
        ledBlinkState = false;
        ledResting = false;
        lastProtocolLedTime = millis();
    }
}

SPIClass SPI_Sensing(VSPI);
uint8_t spi_modes[] = {SPI_MODE0, SPI_MODE1, SPI_MODE2, SPI_MODE3};
volatile bool estaConfigurando = false;
String bufferM2S = "";
String bufferS2M = "";
unsigned long lastFlushTime = 0;
unsigned long lastBlinkTime = 0;
bool blinkState = false;
unsigned long greenLedTurnOffTime = 0;
unsigned long whiteLedOffTime = 0;
unsigned long redLedOffTime = 0;
unsigned long blueLedOffTime = 0;
const int FLUSH_INTERVAL = 20;
 
 // --- VARIABLES PARA SNIFFING SPI ---
 // Usamos un buffer circular de 512 bytes para agrupar bytes antes de enviarlos por WiFi y evitar saturación.
 #define SPI_BUFFER_SIZE 512
 volatile uint8_t spiRawBuffer[SPI_BUFFER_SIZE];
 volatile int spiWriteIdx = 0;
 int spiReadIdx = 0;
 
 volatile uint8_t spiByterx = 0;
 volatile uint8_t spiBytetx = 0;
 volatile int spiBitCnt = 0;
 
 // Lectura ultra-rápida de registros para SPI (Acceso directo al hardware del ESP32)
 // REG_READ(GPIO_IN_REG) lee el estado de todos los pines en un solo ciclo de reloj.
 #define READ_MOSI_FAST ((REG_READ(GPIO_IN_REG) >> 23) & 1)
 #define READ_MISO_FAST ((REG_READ(GPIO_IN_REG) >> 19) & 1)
 #define READ_CS_FAST   ((REG_READ(GPIO_IN_REG) >> 5) & 1)
 
 // ISR (Rutina de Interrupción): Se ejecuta cada vez que el reloj SPI (SCK) sube.
 // Se almacena en la memoria IRAM para máxima velocidad de respuesta.
 void IRAM_ATTR onSCKRising() {
     // Solo capturamos si el Chip Select (CS) está en nivel bajo (activo)
     if (READ_CS_FAST == 0) { 
         // Construimos el byte desplazando los bits hacia la izquierda (MSB first)
         spiByterx = (spiByterx << 1) | READ_MOSI_FAST;
         spiBytetx = (spiBytetx << 1) | READ_MISO_FAST;
         spiBitCnt++;
         
         // Al completar 8 bits, guardamos los bytes capturados (MOSI y MISO) en el buffer circular
         if (spiBitCnt == 8) {
             spiRawBuffer[spiWriteIdx] = spiByterx; // Guardar MOSI
             spiWriteIdx = (spiWriteIdx + 1) % SPI_BUFFER_SIZE;
             spiRawBuffer[spiWriteIdx] = spiBytetx; // Guardar MISO
             spiWriteIdx = (spiWriteIdx + 1) % SPI_BUFFER_SIZE;
             
             spiBitCnt = 0;
             spiByterx = 0;
             spiBytetx = 0;
         }
     } else {
         spiBitCnt = 0; // Si CS sube antes de tiempo, reiniciamos el conteo
         spiByterx = 0;
         spiBytetx = 0;
     }
 }

void wsSend(String texto) {
    if (ws.count() > 0) {
        String payload = texto + "\n";
        ws.textAll(payload);
    }
}

//  Interpretación de comandos que llegan desde la página web vía WebSocket
void ejecutarComando(String cmd) {
    Serial.print(">> Comando WS Recibido: "); Serial.println(cmd);
    
    //  PING: verifica la latencia y confirma que la conexión está viva
    if (cmd == "PING") { 
        wsSend("PONG"); 
        digitalWrite(LED_ERROR_VERDE, HIGH);
        greenLedTurnOffTime = millis() + 500; 
        return; 
    }
    
    //  CONFIG: Configuración de modo de operación (RS232, I2C, SPI)
    //  Parámetros: Proto = RS232 | I2C | SPI; Val = Baudrate | Velocidad | Modo (0-3)
    if (cmd.startsWith("CONFIG:")) {
        estaConfigurando = true;
        int first = cmd.indexOf(':');
        int second = cmd.indexOf(':', first + 1);
        String proto = cmd.substring(first + 1, second);
        String val = cmd.substring(second + 1);

        if (proto == "RS232") {

            //  RS232: Configuración de baudrate
            long baud = val.toInt();
            Serial.println("\n[CONFIG] >>> Modo UART Proxy Activado <<<");
            Serial.print("[CONFIG] Baudrate: "); Serial.println(baud);
            Serial.println("[CONFIG] Pines CH1 (Master): RX=25, TX=26");
            Serial.println("[CONFIG] Pines CH2 (Slave):  RX=33, TX=32");
            
            Serial1.end(); Serial2.end();
            // Inicializacion de puertos seriales para UART proxy
            Serial1.begin(baud, SERIAL_8N1, 16, 17);
            Serial2.begin(baud, SERIAL_8N1, 33, 32); 
            
            //  LED rojo parpadea según baudrate
            int blinks = 1;
            if (baud == 19200) blinks = 2;
            else if (baud == 38400) blinks = 3;
            else if (baud == 57600) blinks = 4;
            else if (baud == 115200) blinks = 5;
            actualizarLedsProtocolo("RS232", blinks);

        } else if (proto == "I2C") {

            //  I2C: Configuración de velocidad (100kHz o 400kHz)
            long speed = val.toInt();
            Serial.println(">> [CONFIG] Modo I2C configurado. Velocidad: " + String(speed));
            actualizarLedsProtocolo("I2C", (speed == 400000) ? 2 : 1);

        } else if (proto == "SPI") {
            
            //  SPI: Configuración de modo (0-3)
            //  Aqui solo escuchamos cuando se active CS, por eso no necesitamos configurar las interrupciones de cada pin
            Serial.println(">> [CONFIG] Activando Sniffer SPI por Interrupciones...");
            // Desactivacion de interrupciones previas y finalizacion de SPI
            detachInterrupt(digitalPinToInterrupt(18));
            SPI_Sensing.end();
            // Configuración de pines como entradas para lectura
            pinMode(18, INPUT); // SCK
            pinMode(23, INPUT); // MOSI
            pinMode(19, INPUT); // MISO
            pinMode(5, INPUT);  // CS
            // Reseteo de variables SPI
            spiBitCnt = 0;
            spiReadIdx = 0;
            spiWriteIdx = 0;
            // Activación de interrupciones para el pin 18 (SCK)
            attachInterrupt(digitalPinToInterrupt(18), onSCKRising, RISING);
            // Indicador de modo SPI
            actualizarLedsProtocolo("SPI", 0);
        }

 else if (proto == "PROXY") {
            // UART siempre opera en modo proxy
            Serial.println(">> [INFO] UART siempre en modo proxy transparente.");
        }
        
        delay(50); 
        wsSend("CONFIG_OK");
        estaConfigurando = false;
    }
}

//  Interpretación de comandos enviados desde la página web vía WebSocket
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf(">> [WiFi] Cliente conectado a WebSockets (ID: %u)\n", client->id());
        client->text("Conectado al Visualizador SerialScope");
        // Al conectar: encender LED blanco y programar su apagado (5 seg)
        digitalWrite(LED_WIFI_BLANCO, HIGH);
        whiteLedOffTime = millis() + 5000;
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf(">> [WiFi] Cliente desconectado (ID: %u)\n", client->id());
        actualizarLedsProtocolo("", 0); // Apagar todo cuando se vaya el cliente
    } else if (type == WS_EVT_DATA) {
        //  Procesamiento de datos enviados desde la página web vía WebSocket
        AwsFrameInfo *info = (AwsFrameInfo*)arg;
        if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
            String msg = "";
            for(size_t i=0; i<len; i++) {
                msg += (char)data[i];
            }
            msg.trim();
            ejecutarComando(msg);
        }
    }
}
 
 // --- VARIABLES PARA SNIFFING I2C ---
 #define I2C_BUFFER_SIZE 1024
 volatile uint8_t i2cRawBuffer[I2C_BUFFER_SIZE]; // Almacén circular para los bytes capturados
 volatile int i2cWriteIdx = 0;                  // Dónde escribe el sniffer (interrupciones)
 int i2cReadIdx = 0;                           // Dónde lee el programa principal (loop)
 volatile unsigned long lastI2cTime = 0;       // Marca de tiempo para detectar fin de ráfaga
 volatile bool i2cStarted = false;             // Flag que indica si hay una comunicación activa
 volatile int i2cBitCount = 0;                 // Contador de bits (0 a 8)
 volatile uint8_t i2cCurrentByte = 0;          // Byte que se está construyendo bit a bit
 
 // Lectura ultra-rápida de registros para I2C
 #define READ_SCL_FAST ((REG_READ(GPIO_IN_REG) >> 22) & 1)
 #define READ_SDA_FAST ((REG_READ(GPIO_IN_REG) >> 21) & 1)
 
 // ISR: Se activa cuando cambia el cable de DATOS (SDA)
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
 
 // ISR: Se activa cada vez que el reloj (SCL) sube
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

void setup() {
    //  Inicialización de pines de LEDs y comunicación serial para serial monitor del IDE Arduino
    Serial.begin(115200);
    pinMode(LED_MODO_ROJO, OUTPUT);
    pinMode(LED_MODO_AZUL, OUTPUT);
    pinMode(LED_MODO_AMARILLO, OUTPUT);
    pinMode(LED_WIFI_BLANCO, OUTPUT);
    pinMode(LED_ERROR_VERDE, OUTPUT);
    actualizarLedsProtocolo("", 0);
    digitalWrite(LED_WIFI_BLANCO, LOW);
    digitalWrite(LED_ERROR_VERDE, LOW);

    WiFi.softAP(ssid, password);
    digitalWrite(LED_WIFI_BLANCO, HIGH);
    
    ws.onEvent(onEvent);
    server.addHandler(&ws);
    server.begin();

    // Configuración de Pines Seriales (Sin pinMode previo para evitar conflictos con el driver UART)
    Serial1.begin(115200, SERIAL_8N1, 16, 17); // RX: 16, TX: 17 (Lado Master - Nativo UART2)
    Serial2.begin(115200, SERIAL_8N1, 33, 32); // RX: 33, TX: 32 (Lado Slave)
    
    SPI_Sensing.begin(18, 19, 23, 5);

    // Restaurar LEDs como OUTPUT tras inicializar SPI
    pinMode(LED_MODO_AMARILLO, OUTPUT);
    pinMode(LED_MODO_AZUL, OUTPUT);
    pinMode(LED_MODO_ROJO, OUTPUT);
    digitalWrite(LED_MODO_ROJO, LOW);

    // Configurar interrupciones I2C (estos pines sí necesitan pinMode)
    pinMode(21, INPUT_PULLUP);
    pinMode(22, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(22), onSCLRising, RISING);
    attachInterrupt(digitalPinToInterrupt(21), onSDAChange, CHANGE);

    Serial.println("Visualizador SerialScope WiFi Online.");
    
    // Optimización: Reservar memoria para los buffers UART para evitar fragmentación
    bufferM2S.reserve(256);
    bufferS2M.reserve(256);
}

void loop() {
    ws.cleanupClients();
    
    // Lógica del LED Blanco: Parpadea si no hay clientes. 
    // Si hay clientes, se queda encendido 5 segundos y luego se apaga.
    if (ws.count() == 0) {
        if (millis() - lastBlinkTime > 500) {
            blinkState = !blinkState;
            digitalWrite(LED_WIFI_BLANCO, blinkState ? HIGH : LOW);
            lastBlinkTime = millis();
        }
        whiteLedOffTime = 0; // Reiniciar para la próxima conexión
    } else {
        // Hay clientes conectados
        if (whiteLedOffTime > 0) {
            if (millis() >= whiteLedOffTime) {
                digitalWrite(LED_WIFI_BLANCO, LOW); // Apagar tras el tiempo
                whiteLedOffTime = 0; 
            } else {
                digitalWrite(LED_WIFI_BLANCO, HIGH); // Mantener encendido
            }
        }
    }

    // Apagar temporalmente el LED verde tras un PONG
    if (greenLedTurnOffTime > 0 && millis() > greenLedTurnOffTime) {
        digitalWrite(LED_ERROR_VERDE, LOW);
        greenLedTurnOffTime = 0;
    }
    if (redLedOffTime > 0 && millis() > redLedOffTime) {
        digitalWrite(LED_MODO_ROJO, LOW);
        redLedOffTime = 0;
    }
    if (blueLedOffTime > 0 && millis() > blueLedOffTime) {
        digitalWrite(LED_MODO_AZUL, LOW);
        blueLedOffTime = 0;
    }

    // Lógica de parpadeo de LEDs: solo UART/RS232 parpadea, I2C y SPI son fijos (ya se encienden en actualizarLedsProtocolo)
    if (currentProtocol == "RS232" && ledBlinkTarget > 0) {
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
        
        // Efecto visual de actividad
        digitalWrite(LED_MODO_AMARILLO, HIGH);
        redLedOffTime = millis() + 20;
    }



    if (estaConfigurando) return;

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
        if (bufferS2M.length() > 500) break;
    }

    // --- SNIFFING I2C (Procesamiento de Buffer) ---
    // Al igual que en SPI, extraemos los datos capturados por las interrupciones
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
            Serial.println(">> [WiFi] Enviando Buffer CH1 -> CH2");
            wsSend("RS232_RECV_BUF:M2S:" + bufferM2S);
            bufferM2S = "";
        }
        if (bufferS2M.length() > 0) {
            Serial.println("<< [WiFi] Enviando Buffer CH2 -> CH1");
            wsSend("RS232_RECV_BUF:S2M:" + bufferS2M);
            bufferS2M = "";
        }
        lastFlushTime = millis();
    }
}
