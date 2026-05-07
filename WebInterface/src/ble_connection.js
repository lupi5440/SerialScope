/**
 * UUIDs estándar usados para UART a través de BLE (Nordic UART Service)
 */
const BLE_SERVICE_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const BLE_RX_UUID = '6e400002-b5a3-f393-e0a9-e50e24dcca9e';
const BLE_TX_UUID = '6e400003-b5a3-f393-e0a9-e50e24dcca9e';

export class BLEProxy {
    constructor(role) {
        this.role = role;
        this.device = null;
        this.server = null;
        this.service = null;
        this.rxCharacteristic = null;
        this.txCharacteristic = null;

        this.onConnect = null;
        this.onDisconnect = null;
        this.onDataReceived = null;

        this.encoder = new TextEncoder();
        this.decoder = new TextDecoder();
        this.buffer = "";
    }

    /**
     * Solicita conexión al dispositivo vía Bluetooth Low Energy
     */
    async connect() {
        if (!navigator.bluetooth) {
            alert('La Web Bluetooth API no está soportada en este navegador. Utiliza Google Chrome en Windows/Android.');
            return false;
        }

        try {
            console.log(`[${this.role}] Buscando dispositivo BLE...`);

            this.device = await navigator.bluetooth.requestDevice({
                // Filtramos por dispositivos que se llamen SerialScope o tengan el servicio de UART
                filters: [
                    { namePrefix: 'SerialScope' }
                ],
                optionalServices: [BLE_SERVICE_UUID]
            });

            this.device.addEventListener('gattserverdisconnected', () => this.handleDisconnect());

            console.log(`[${this.role}] Conectando al Servidor GATT...`);
            this.server = await this.device.gatt.connect();

            console.log(`[${this.role}] Obteniendo Servicio Principal...`);
            this.service = await this.server.getPrimaryService(BLE_SERVICE_UUID);

            console.log(`[${this.role}] Configurando Características (RX/TX)...`);
            this.rxCharacteristic = await this.service.getCharacteristic(BLE_RX_UUID);
            this.txCharacteristic = await this.service.getCharacteristic(BLE_TX_UUID);

            // Activamos las notificaciones para escuchar cuando el ESP envíe algo
            await this.txCharacteristic.startNotifications();
            this.txCharacteristic.addEventListener('characteristicvaluechanged', (e) => this.handleNotifications(e));

            console.log(`[${this.role}] ¡Conectado existosamente a ${this.device.name}!`);
            if (this.onConnect) this.onConnect();
            return true;

        } catch (error) {
            console.error(`[${this.role}] Error de conexión BLE:`, error);
            if (error.name !== 'NotFoundError') { // El usuario no canceló el popup
                alert(`Error al conectar con el ${this.role}. Asegúrate que esté encendido y cerca.`);
            }
            return false;
        }
    }

    /**
     * Función llamada internamente cuando el ESP32 se desconecta o se apaga
     */
    handleDisconnect() {
        console.warn(`[${this.role}] Dispositivo desconectado.`);
        this.device = null;
        this.server = null;
        if (this.onDisconnect) this.onDisconnect();
    }

    /**
     * Recibe los paquetes BLE desde el ESP32
     */
    handleNotifications(event) {
        const value = event.target.value;
        const chunk = this.decoder.decode(value);
        this.buffer += chunk;

        // Extraer líneas completas
        let lines = this.buffer.split('\n');
        this.buffer = lines.pop(); // la última podría estar incompleta

        for (let line of lines) {
            line = line.trim();
            if (line.length > 0) {
                console.log(`[${this.role} Data] ${line}`);
                if (this.onDataReceived) {
                    this.onDataReceived(line);
                }
            }
        }
    }

    /**
     * Enviar comandos de texto al ESP32 por BLE
     * @param {string} data El comando o mensaje a enviar
     */
    async sendData(data) {
        if (!this.rxCharacteristic) {
            console.error(`[${this.role}] No se puede enviar, no hay conexión BLE activa.`);
            return;
        }

        // El ESP32 espera que cada comando termine con un salto de línea (\n)
        let encoded = this.encoder.encode(data + "\n");

        try {
            await this.rxCharacteristic.writeValue(encoded);
        } catch (error) {
            console.error(`[${this.role}] Error enviando datos:`, error);
        }
    }
    async disconnect() {
        if (this.device && this.device.gatt.connected) {
            console.log(`[${this.role}] Desconectando manualmente...`);
            this.device.gatt.disconnect();
        }
    }
}

// Instancias globales para el visualizador y emulador
export const bleAnalizador = new BLEProxy("Analizador");
export const bleEmulador = new BLEProxy("Emulador");
