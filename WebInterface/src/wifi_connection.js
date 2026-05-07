/**
 * Gestor de conexión vía WebSockets para SerialScope
 */
export class WiFiProxy {
    constructor(role) {
        this.role = role;
        this.socket = null;
        this.ip = "192.168.4.1"; // IP predeterminada en modo AP del ESP32

        this.onConnect = null;
        this.onDisconnect = null;
        this.onDataReceived = null;

        this.buffer = "";
    }

    /**
     * Getter para compatibilidad con el código anterior
     */
    get isConnected() {
        return this.socket && this.socket.readyState === WebSocket.OPEN;
    }

    /**
     * Inicia la conexión WebSocket con el ESP32
     */
    connect(customIp) {
        if (customIp) this.ip = customIp;

        const url = `ws://${this.ip}/ws`;
        console.log(`[${this.role}] Conectando a WebSocket en ${url}...`);

        try {
            this.socket = new WebSocket(url);

            this.socket.onopen = () => {
                console.log(`[${this.role}] ¡Conexión WiFi/WS establecida!`);
                if (this.onConnect) this.onConnect();
            };

            this.socket.onclose = () => {
                console.warn(`[${this.role}] Conexión cerrada.`);
                this.socket = null;
                if (this.onDisconnect) this.onDisconnect();
            };

            this.socket.onerror = (error) => {
                console.error(`[${this.role}] Error en WebSocket:`, error);
                alert(`No se pudo conectar al Visualizador en ${this.ip}. Verifica que estés conectado a la red WiFi 'SerialScope_Visualizador'.`);
            };

            this.socket.onmessage = (event) => {
                this.handleMessage(event.data);
            };

        } catch (error) {
            console.error(`[${this.role}] Error al crear WebSocket:`, error);
        }
    }

    /**
     * Procesa los datos recibidos
     */
    handleMessage(data) {
        // Los WebSockets entregan el mensaje completo tal cual lo envía el servidor
        let lines = data.split('\n');
        for (let line of lines) {
            line = line.trim();
            if (line.length > 0) {
                if (this.onDataReceived) {
                    this.onDataReceived(line);
                }
            }
        }
    }

    /**
     * Envía comandos de texto al ESP32 por WiFi
     * @param {string} data El comando o mensaje a enviar
     */
    sendData(data) {
        if (!this.socket || this.socket.readyState !== WebSocket.OPEN) {
            console.error(`[${this.role}] No se puede enviar, el WebSocket no está abierto.`);
            return;
        }

        // El ESP32 espera que cada comando termine con un salto de línea (\n)
        this.socket.send(data + "\n");
    }

    /**
     * Cierra la conexión
     */
    disconnect() {
        if (this.socket) {
            this.socket.close();
        }
    }
}

// Instancia global para el analizador (Proxy)
export const wifiAnalizador = new WiFiProxy("Analizador");
