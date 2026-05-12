//LÓGICA DE CONEXIÓN BLE

const BLE_SERVICE_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const BLE_RX_UUID = '6e400002-b5a3-f393-e0a9-e50e24dcca9e';
const BLE_TX_UUID = '6e400003-b5a3-f393-e0a9-e50e24dcca9e';

class BLEProxy {
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

    async connect() {
        if (!navigator.bluetooth) {
            alert('La Web Bluetooth API no está soportada. Usa Chrome.');
            return false;
        }

        try {
            // El requestDevice SOLO puede llamarse tras un clic y no puede estar en un loop con delays
            if (!this.device) {
                this.device = await navigator.bluetooth.requestDevice({
                    filters: [{ namePrefix: 'SerialScope' }],
                    optionalServices: [BLE_SERVICE_UUID]
                });
                this.device.addEventListener('gattserverdisconnected', () => this.handleDisconnect());
            }

            let retries = 2;
            while (retries > 0) {
                if (!this.device) break; // Seguridad: Si el dispositivo se perdió, salir del loop

                try {
                    console.log(`[${this.role}] Intentando conectar al GATT...`);
                    this.server = await this.device.gatt.connect();

                    if (!this.server) throw new Error("No se pudo obtener el servidor GATT");

                    console.log(`[${this.role}] GATT conectado, obteniendo servicios...`);

                    // Pausa de seguridad para estabilizar la conexión BLE
                    await new Promise(resolve => setTimeout(resolve, 500));

                    if (!this.device || !this.device.gatt.connected) throw new Error("Conexión perdida durante handshake");

                    this.service = await this.server.getPrimaryService(BLE_SERVICE_UUID);
                    this.rxCharacteristic = await this.service.getCharacteristic(BLE_RX_UUID);
                    this.txCharacteristic = await this.service.getCharacteristic(BLE_TX_UUID);

                    await this.txCharacteristic.startNotifications();
                    this.txCharacteristic.addEventListener('characteristicvaluechanged', (e) => this.handleNotifications(e));

                    console.log(`[${this.role}] ¡Conexión completa!`);
                    if (this.onConnect) this.onConnect();
                    return true;
                } catch (gattError) {
                    console.warn(`[${this.role}] Error en handshake GATT:`, gattError);
                    retries--;
                    if (this.server) {
                        try { await this.device.gatt.disconnect(); } catch (e) { }
                    }
                    if (retries > 0) await new Promise(resolve => setTimeout(resolve, 250));
                    else throw gattError;
                }
            }
        } catch (error) {
            console.error(`[${this.role}] Error fatal:`, error);
            if (error.name !== 'NotFoundError') {
                alert(`Error de conexión: ${error.message}`);
            }
            return false;
        }
    }

    handleDisconnect() {
        this.device = null;
        this.server = null;
        if (this.onDisconnect) this.onDisconnect();
    }

    handleNotifications(event) {
        const value = event.target.value;
        const chunk = this.decoder.decode(value);
        this.buffer += chunk;
        let lines = this.buffer.split('\n');
        this.buffer = lines.pop();
        for (let line of lines) {
            line = line.trim();
            if (line.length > 0 && this.onDataReceived) this.onDataReceived(line);
        }
    }

    async sendData(data) {
        if (!this.rxCharacteristic) return;
        try {
            await this.rxCharacteristic.writeValue(this.encoder.encode(data + "\n"));
        } catch (error) { console.error("Error envío:", error); }
    }

    async disconnect() {
        if (this.device && this.device.gatt.connected) this.device.gatt.disconnect();
    }
}

export const bleEmulador = new BLEProxy("Emulador");
export const bleDestino = new BLEProxy('UART Dest');

let bmpCalib = null;
let wizardStep = 0;

export function conectarEmulador() {
    const statusEl = document.getElementById('status-emulador');
    const btnConectar = document.getElementById('btn-conectar-emulador');
    const pingBtn = document.getElementById('btn-ping-emulador');
    const protoSelect = document.getElementById('protocoloEmuSelect');
    const initBtn = document.getElementById('btn-iniciar-emulacion');

    if (!statusEl) return;

    // Disparador cuando el Device Bluetooth contesta de éxito
    bleEmulador.onConnect = () => {
        statusEl.innerText = "CONECTADO";
        statusEl.className = "badge bg-success mb-3 p-2 px-3";
        if (btnConectar) {
            btnConectar.disabled = true;
            btnConectar.innerHTML = `<i class="bi bi-link"></i> Master Vinculado`;
        }
        pingBtn.disabled = false;
        initBtn.disabled = false;

        // Desbloquear panel de configuración interactivamente (quita visibilidad "borrosa" de Bootstrap)
        const configPanel = document.getElementById('panel-config-emu');
        if (configPanel) {
            configPanel.classList.remove('opacity-50');
            configPanel.style.pointerEvents = 'auto'; // Habilita clicks!
        }

        alert('✅ Master Conectado.');
    };

    // Disparador de Pérdida de Enlace o apagado involuntario
    bleEmulador.onDisconnect = () => {
        statusEl.innerText = "DESCONECTADO";
        statusEl.className = "badge bg-secondary mb-3 p-2 px-3";
        if (btnConectar) {
            btnConectar.disabled = false;
            btnConectar.innerHTML = `<i class="bi bi-link-45deg"></i> Conectar Master`;
        }
        pingBtn.disabled = true;
        initBtn.disabled = true;

        // Bloquear panel de configuración (Restringir la UI de nuevo protectivamente)
        const configPanel = document.getElementById('panel-config-emu');
        if (configPanel) {
            configPanel.classList.add('opacity-50');
            configPanel.style.pointerEvents = 'none'; // Evitar clicks
        }
    };

    // Función que lee los reportes del micro ESP32
    bleEmulador.onDataReceived = (data) => {
        console.log("Respuesta del Emulador:", data);
        if (data.includes("PONG")) {
            // Confirmación de latencia visual
            alert("¡PONG! Pruebas Master respondiendo correctamente.");
            return;
        }
        if (data.startsWith("READY:")) {
            // ¡Confirmado! El hardware terminó de remapear puertos.
            marcarEmuSincronizado();
            return;
        }

        // Si es un resultado de sensor (I2C/SPI)
        if (data.includes("RESULTADO:")) {
            const rawVal = data.replace("RESULTADO:", "").trim();
            const sensorVal = document.getElementById('sensor-value');
            const sensorRaw = document.getElementById('sensor-raw-value');
            const rawContainer = document.getElementById('raw-data-container');
            const sensorStatus = document.getElementById('sensor-status');

            const profile = document.getElementById('emu-i2c-profile')?.value ||
                document.getElementById('emu-spi-profile')?.value;

            if (sensorVal) {
                if (rawContainer) rawContainer.style.display = 'block';

                let displayVal = rawVal;
                let statusText = "Dato del Bus";
                let meaning = "";

                if (rawVal.includes(":")) {
                    const parts = rawVal.split(":");
                    statusText = parts[0].trim();
                    displayVal = parts[1].trim();
                }

                // Limpiar el valor RAW para que solo sea el dato (sin el texto de "Dato del Sensor...")
                if (sensorRaw) sensorRaw.innerText = displayVal;

                // --- LÓGICA DE CALIBRACIÓN BMP180 ---
                if (profile === "BMP180") {
                    // Si recibimos una ráfaga larga (22 bytes en hex separados por coma o espacio)
                    const hexList = rawVal.split(/[\s,]+/).filter(x => x.length > 0);

                    if (hexList.length === 22) {
                        // Fase de CALIBRACIÓN: Extraer los 11 shorts
                        const readShort = (idx) => {
                            const high = parseInt(hexList[idx * 2], 16);
                            const low = parseInt(hexList[idx * 2 + 1], 16);
                            const val = (high << 8) | low;
                            return val > 32767 ? val - 65536 : val;
                        };
                        const readUShort = (idx) => {
                            const high = parseInt(hexList[idx * 2], 16);
                            const low = parseInt(hexList[idx * 2 + 1], 16);
                            return (high << 8) | low;
                        };

                        bmpCalib = {
                            ac1: readShort(0), ac2: readShort(1), ac3: readShort(2),
                            ac4: readUShort(3), ac5: readUShort(4), ac6: readUShort(5),
                            b1: readShort(6), b2: readShort(7), mb: readShort(8),
                            mc: readShort(9), md: readShort(10)
                        };
                        console.log("BMP180 Calibrado en Pruebas:", bmpCalib);
                        meaning = "✅ Calibración Recibida. Iniciando lectura...";
                        // Una vez calibrado, podemos pedir el dato de temperatura
                        setTimeout(() => { bleEmulador.sendData("EMU_START"); }, 500);
                    }
                    else if (bmpCalib && !isNaN(parseInt(displayVal))) {
                        // Fase de LECTURA: Fórmula Bosch
                        const ut = parseInt(displayVal);
                        let x1 = (ut - bmpCalib.ac6) * bmpCalib.ac5 / 32768;
                        let x2 = (bmpCalib.mc * 2048) / (x1 + bmpCalib.md);
                        let b5 = x1 + x2;
                        let t = (b5 + 8) / 16;
                        displayVal = (t / 10).toFixed(1) + " °C";
                        meaning = "Temperatura Calibrada (Fórmula Bosch).";
                    } else {
                        meaning = "Esperando Temperatura Cruda (UT)...";
                    }
                } else if (profile === "TMP" && !isNaN(parseInt(displayVal))) {
                    const digitalVal = parseInt(displayVal);
                    const celsius = (digitalVal * 0.0625).toFixed(2);
                    displayVal = celsius + " °C";
                    meaning = "Temperatura Calculada (Digital * 0.0625).";
                } else if (profile === "MAX6675") {
                    meaning = "Lectura del Termopar (Unidad en °C).";
                }

                sensorVal.innerText = displayVal;
                if (sensorStatus) {
                    sensorStatus.innerHTML = `<strong>Contexto:</strong> ${statusText}<br><span class="text-success"><strong>Significado:</strong> ${meaning}</span>`;
                }
            }
            return;
        }

        // Mostrar mensajes recibidos
        const masterChat = document.getElementById('chat-master-recibido');
        if (masterChat) {
            masterChat.value += data + "\n";
            masterChat.scrollTop = masterChat.scrollHeight;
        }
    };

    // Activar Popup Nativo de Google Chrome / Edge
    bleEmulador.connect();
}

/**
 * Control de UI Dinámico
 */
export function cambiarVistaConfig() {
    const protoSelect = document.getElementById('protocoloEmuSelect');
    const paramsContainer = document.getElementById('emu-params-container');
    const sensorContainer = document.getElementById('emu-sensor-container');
    const interactionSensor = document.getElementById('interaction-sensor');
    const interactionManual = document.getElementById('interaction-manual');
    const panelUartDestino = document.getElementById('panel-uart-destino');
    const panelChatDestino = document.getElementById('panel-chat-destino');

    if (!protoSelect) return;

    const proto = protoSelect.value;
    const isAutoMode = (proto !== 'UART'); // I2C and SPI are auto sensor. UART is manual chat.

    // Siempre marcar como desincronizado al cambiar de vista o inicializar
    marcarEmuDesincronizado();

    // 1. Mostrar/Ocultar áreas principales
    if (isAutoMode) {
        // Si cambiamos a I2C o SPI, forzamos la desconexión del Slave UART por seguridad
        if (bleDestino && bleDestino.device) {
            bleDestino.disconnect();
        }
        if (sensorContainer) sensorContainer.style.display = 'block';
        if (interactionSensor) interactionSensor.style.display = 'block';

        // Resetear visibilidad de los rows de configuración (TFT vs Estándar)
        const stdRow = document.getElementById('emu-standard-config-row');
        const fullRow = document.getElementById('emu-full-width-config-row');
        if (stdRow) stdRow.style.display = 'flex';
        if (fullRow) fullRow.style.display = 'none';

        if (interactionManual) interactionManual.style.display = 'none';
        if (panelUartDestino) panelUartDestino.style.display = 'none';
        if (panelChatDestino) panelChatDestino.style.display = 'none';
    } else {
        if (sensorContainer) sensorContainer.style.display = 'none';
        if (interactionSensor) interactionSensor.style.display = 'none';
        if (interactionManual) interactionManual.style.display = 'block';

        if (panelUartDestino) panelUartDestino.style.display = 'block';
        if (panelChatDestino) panelChatDestino.style.display = 'block';
    }

    // 2. Inyectar Parámetros del Bus
    let paramsHTML = '';
    if (proto === 'UART') {
        paramsHTML = `
            <div class="mb-3">
                <label class="form-label fw-bold small text-muted">Velocidad (Baudios)</label>
                <select id="emu-baud" class="form-select border-2 border-success border-opacity-50" onchange="marcarEmuDesincronizado()">
                    <option value="9600">9600 bps</option>
                    <option value="19200">19200 bps</option>
                    <option value="38400">38400 bps</option>
                    <option value="57600">57600 bps</option>
                    <option value="115200" selected>115200 bps</option>
                </select>
            </div>`;
    } else if (proto === 'I2C') {
        const profile = document.getElementById('emu-i2c-profile')?.value || "BMP180";
        const initialAddr = (profile === "TMP") ? "0x48" : "0x77";
        const isReadOnly = (profile === "BMP180") ? "readonly" : "";

        paramsHTML = `
            <div class="row g-2 mb-3">
                <div class="col-6">
                    <label class="form-label fw-bold small text-muted mb-1">
                        <i class="bi bi-speedometer2 me-1"></i>Velocidad
                    </label>
                    <input type="text" id="emu-i2c-speed" class="form-control border-2 border-success border-opacity-50 font-monospace bg-light shadow-none" value="100000" readonly>
                </div>
                <div class="col-6">
                    <label class="form-label fw-bold small text-muted mb-1">
                        <i class="bi bi-hash me-1"></i>Dirección (Hex)
                    </label>
                    <input type="text" id="emu-i2c-addr" class="form-control border-2 border-success border-opacity-50 font-monospace bg-light shadow-none" value="${initialAddr}" ${isReadOnly} onchange="marcarEmuDesincronizado()">
                </div>
            </div>`;

        if (profile === "TMP") {
            paramsHTML += `
            <div class="p-3 bg-white border border-success border-opacity-25 rounded-4 shadow-sm animate__animated animate__fadeInLeft">
                <div class="small fw-bold text-success mb-2"><i class="bi bi-pin-map-fill me-1"></i> Configuración Pin ADD0</div>
                <div class="row g-2 text-center small">
                    <div class="col-6 border-end border-success border-opacity-10">
                        <div class="text-muted" style="font-size: 0.7rem;">ADD0 → GND</div>
                        <div class="fw-bold font-monospace text-success">0x48</div>
                    </div>
                    <div class="col-6">
                        <div class="text-muted" style="font-size: 0.7rem;">ADD0 → VCC</div>
                        <div class="fw-bold font-monospace text-success">0x49</div>
                    </div>
                </div>
            </div>`;
        }
    } else if (proto === 'SPI') {
        paramsHTML = `
            <div class="mb-3">
                <label class="form-label fw-bold small text-muted">Modo SPI</label>
                <input type="text" id="emu-spi-mode" class="form-control border-2 border-success border-opacity-50 font-monospace bg-light" value="Modo 0 (CPOL=0, CPHA=0)" readonly data-mode="0">
            </div>`;
    }
    if (paramsContainer) paramsContainer.innerHTML = paramsHTML;

    // 3. Inyectar Selector de Sensor con Iconos
    if (isAutoMode && sensorContainer) {
        let sensorHTML = '';
        if (proto === 'UART') {
            sensorHTML = `
                <div class="mb-3">
                    <label class="form-label fw-bold small text-muted">Perfil de Datos</label>
                    <select id="emu-uart-msg" class="form-select border-2 border-success border-opacity-50" onchange="marcarEmuDesincronizado()">
                        <option value="MAX3232">🔌 Módulo MAX3232 (Prueba Comunicación UART)</option>
                    </select>
                </div>`;
        } else if (proto === 'I2C') {
            const currentProfile = document.getElementById('emu-i2c-profile')?.value || "BMP180";
            sensorHTML = `
                <div class="mb-3">
                    <label class="form-label fw-bold small text-muted">
                        <i class="bi bi-cpu-fill me-1"></i>Perfil de Sensor
                    </label>
                    <select id="emu-i2c-profile" class="form-select border-2 border-success border-opacity-50 shadow-none" onchange="cambiarPerfilI2C()">
                        <option value="BMP180" ${currentProfile === 'BMP180' ? 'selected' : ''}>☁️ Sensor Barométrico (BMP180)</option>
                        <option value="TMP" ${currentProfile === 'TMP' ? 'selected' : ''}>🌡️ Sensor Temperatura (TMP102)</option>
                    </select>
                </div>`;

            // Si es TMP102, añadir guía de dirección y botón de alarma
            if (currentProfile === "TMP") {
                sensorHTML += `
                <div class="p-3 bg-white border border-success border-opacity-25 rounded-4 shadow-sm animate__animated animate__fadeInRight">
                    <label class="form-label fw-bold small text-success mb-2">
                        <i class="bi bi-bell-fill me-1"></i> Configurar Alarma (°C)
                    </label>
                    <div class="input-group input-group-sm">
                        <input type="number" id="tmp-alarm-val" class="form-control border-success border-opacity-25" value="40" min="-40" max="125">
                        <button class="btn btn-success fw-bold" type="button" onclick="enviarAlarmaTMP()">
                            SET
                        </button>
                    </div>
                </div>`;
            }
        } else if (proto === 'SPI') {
            const currentProfile = document.getElementById('emu-spi-profile')?.value || "MAX6675";
            sensorHTML = `
                <div class="mb-3">
                    <label class="form-label fw-bold small text-muted">Perfil de Dispositivo</label>
                    <select id="emu-spi-profile" class="form-select border-2 border-success border-opacity-50" onchange="cambiarVistaConfig()">
                        <option value="MAX6675" ${currentProfile === 'MAX6675' ? 'selected' : ''}>🌡️ Sensor de Termopar (MAX6675)</option>
                        <option value="TFT" ${currentProfile === 'TFT' ? 'selected' : ''}>📺 Pantalla TFT SPI (1.8" ST7735)</option>
                    </select>
                </div>`;

            if (currentProfile === "TFT") {
                sensorHTML += `
                <div class="p-4 bg-white border border-primary border-opacity-25 rounded-4 shadow-sm mt-3">
                    <div class="d-flex align-items-center mb-3">
                        <div class="bg-primary bg-opacity-10 p-2 rounded-3 me-3">
                            <i class="bi bi-display text-primary fs-5"></i>
                        </div>
                        <h6 class="mb-0 fw-bold text-primary">Control de Pantalla TFT</h6>
                    </div>
                    <div class="row g-3">
                        <div class="col-12">
                            <label class="form-label fw-bold small text-muted mb-2">Texto a mostrar:</label>
                            <div class="input-group">
                                <input type="text" id="tft-text-input" class="form-control border-primary border-opacity-25 py-2" placeholder="Escribe un mensaje..." maxlength="20">
                                <button class="btn btn-primary fw-bold px-3" type="button" onclick="enviarTextoTFT()">
                                    <i class="bi bi-send-fill"></i>
                                </button>
                            </div>
                        </div>
                        <div class="col-12">
                            <div class="d-flex justify-content-between align-items-center mb-2">
                                <label class="form-label fw-bold small text-muted mb-0">Brillo de Retroiluminación</label>
                                <span id="tft-brightness-val" class="badge bg-primary bg-opacity-10 text-primary">100%</span>
                            </div>
                            <input type="range" class="form-range" id="tft-brightness-range" min="0" max="255" value="255" 
                                oninput="document.getElementById('tft-brightness-val').innerText = Math.round(this.value/2.55) + '%'; setBrilloTFT(this.value)">
                        </div>
                    </div>
                </div>`;
            }
        }
        sensorContainer.innerHTML = sensorHTML;
    }

    // Ajustar layout: en modo TFT expandir paramsCol y sensorCol a col-12 
    // para que ambos paneles ocupen todo el ancho
    const paramsCol = document.getElementById('emu-params-container')?.parentElement;
    const sensorCol = document.getElementById('emu-sensor-container')?.parentElement;
    const spiProfile = document.getElementById('emu-spi-profile')?.value;

    // Ocultar el fullRow (ya no se usa)
    const fullRow = document.getElementById('emu-full-width-config-row');
    if (fullRow) fullRow.style.display = 'none';

    if (proto === 'SPI' && spiProfile === 'TFT') {
        // Expandir a ancho completo
        if (paramsCol) paramsCol.className = 'col-12 mb-2';
        if (sensorCol) sensorCol.className = 'col-12';
        if (interactionSensor) interactionSensor.style.setProperty('display', 'none', 'important');
    } else {
        // Layout estándar 50/50
        if (paramsCol) paramsCol.className = 'col-md-6';
        if (sensorCol) sensorCol.className = 'col-md-6';
        // Solo mostrar el bloque "Iniciar" si es I2C o SPI (no UART que usa su propio panel de chat)
        if (interactionSensor) {
            interactionSensor.style.display = (proto === 'UART') ? 'none' : 'block';
        }
    }
}

/**
 * Actualiza visualmente la dirección I2C en el panel según el sensor elegido
 */
/**
 * Nueva función para cambiar perfil I2C sin romper la UI
 */
window.cambiarPerfilI2C = function () {
    actualizarDireccionI2C();
    cambiarVistaConfig();
}

export function actualizarDireccionI2C() {
    const profile = document.getElementById('emu-i2c-profile')?.value;
    const addrInput = document.getElementById('emu-i2c-addr');
    if (addrInput) {
        addrInput.value = (profile === "TMP") ? "0x48" : "0x77";
        // Si es TMP102, permitir edición manual por si cambiaron el pin ADD0
        addrInput.readOnly = (profile !== "TMP");
    }
    marcarEmuDesincronizado();
}

/**
 * Función especial para el TMP102 que permite escribir el registro de alarma (THIGH - 0x03)
 */
window.enviarAlarmaTMP = function () {
    const temp = parseFloat(document.getElementById('tmp-alarm-val').value);
    const addr = document.getElementById('emu-i2c-addr').value.replace('0x', '');

    // Convertir Temp a formato 12-bit del TMP102
    // Fórmula: (Valor / 0.0625) << 4
    let val = Math.round(temp / 0.0625);
    let hexVal = (val << 4);

    const byteHigh = (hexVal >> 8) & 0xFF;
    const byteLow = hexVal & 0xFF;

    const cmd = `EMU_MSG:I2C:${addr}:03 ${byteHigh.toString(16).padStart(2, '0')} ${byteLow.toString(16).padStart(2, '0')}`;
    bleEmulador.sendData(cmd);

    console.log(`Enviando Alarma TMP102: ${temp}°C -> 0x03 ${byteHigh.toString(16)} ${byteLow.toString(16)}`);
    alert(`Comando de Alarma enviado a 0x${addr} (Registro 0x03): ${temp}°C`);
}

window.actualizarDireccionI2C = actualizarDireccionI2C;

/**
 * Transmisor maestro de perfiles de Hardware.
 * Se encarga de serializar la vista UI elegida (I2C? SPI?, Baudios?) a un código 
 * string legible para la interpretación de C++ en el ESP32, como EMU_CONFIG...
 */
export function aplicarEmulacion() {
    const proto = document.getElementById('protocoloEmuSelect').value;
    const isAutoMode = (proto !== 'UART');
    const modeStr = isAutoMode ? "AUTO" : "MANUAL";

    // Comando inicial de sintaxis base (Protocolo, Modo)
    let cmd = `EMU_CONFIG:${proto}:${modeStr}:`;

    // Cosecha de parámetros específicos según rama elegida
    if (proto === 'UART') {
        if (!bleDestino.device) {
            alert("⚠️ Debes conectar el 'Slave' en el panel de arriba antes de configurar la prueba UART.");
            return;
        }

        const baud = document.getElementById('emu-baud')?.value || "115200";
        const msgType = "MANUAL"; // UART es solo para chat
        cmd += `${baud}:${msgType}`;

        // Enviar también al Destino UART para que se sincronice a la misma velocidad
        bleDestino.sendData(`CFG_BAUD:${baud}`);
    } else if (proto === 'I2C') {
        const profile = document.getElementById('emu-i2c-profile')?.value || "BMP180";
        const addr = (profile === "TMP") ? "0x48" : "0x77";
        const speed = "100000";
        cmd += `${addr}:${speed}:${profile}`;
    } else if (proto === 'SPI') {
        const fullModeSelect = document.getElementById('emu-spi-mode-full');
        const mode = fullModeSelect ? fullModeSelect.value : (document.getElementById('emu-spi-mode')?.value || "0");

        // El perfil puede venir del select estándar o ser TFT si estamos en el panel ancho
        let profile = document.getElementById('emu-spi-profile')?.value;
        const fullRow = document.getElementById('emu-full-width-config-row');
        if (fullRow && fullRow.style.display !== 'none') {
            profile = "TFT";
        }
        if (!profile) profile = "MAX6675";

        cmd += `${mode}:${profile}`;
    }

    const applyBtn = document.getElementById('btn-aplicar-config-emu');
    const initBtn = document.getElementById('btn-iniciar-emulacion');
    if (applyBtn) {
        applyBtn.disabled = true;
        applyBtn.innerHTML = '<span class="spinner-border spinner-border-sm"></span> Sincronizando...';
    }
    if (initBtn) {
        initBtn.disabled = true;
        initBtn.classList.add('opacity-50');
    }

    console.log("Enviando configuración al Emulador:", cmd);
    bleEmulador.sendData(cmd);
}

export function iniciarEmulacion() {
    const profile = document.getElementById('emu-i2c-profile')?.value;

    // Si es BMP180 y no tenemos calibración, la pedimos primero
    if (profile === "BMP180" && !bmpCalib) {
        console.log("Solicitando calibración BMP180...");
        bleEmulador.sendData("EMU_MSG:I2C:READ_CALIB");
        // Nota: El firmware debe responder enviando los 22 bytes en un RESULTADO:
    } else {
        bleEmulador.sendData("EMU_START");
    }

    document.getElementById('btn-iniciar-emulacion').style.display = 'none';
    document.getElementById('btn-detener-emulacion').style.display = 'block';

    const display = document.getElementById('sensor-data-display');
    if (display) display.style.display = 'block';
}

export function enviarMensajeManual() {
    const proto = document.getElementById('protocoloEmuSelect').value;
    const data = document.getElementById('emu-manual-data').value;

    if (!data) {
        alert("⚠️ Por favor ingresa datos antes de enviar.");
        return;
    }

    const cmd = `EMU_MSG:${proto}:${data}`;
    console.log("Enviando mensaje manual:", cmd);
    bleEmulador.sendData(cmd);
}

export function detenerEmulacion() {
    bleEmulador.sendData("EMU_STOP");
    document.getElementById('btn-iniciar-emulacion').style.display = 'block';
    document.getElementById('btn-detener-emulacion').style.display = 'none';

    const display = document.getElementById('sensor-data-display');
    if (display) {
        display.style.display = 'none';
        const sensorVal = document.getElementById('sensor-value');
        if (sensorVal) sensorVal.innerText = "--";
    }
}

export function marcarEmuDesincronizado() {
    const badge = document.getElementById('sync-status-emu');
    const warning = document.getElementById('sync-warning-emu');
    const initBtn = document.getElementById('btn-iniciar-emulacion');
    const manualBtn = document.getElementById('btn-enviar-manual-emu');
    const applyBtn = document.getElementById('btn-aplicar-config-emu');

    if (badge) {
        badge.className = "badge bg-warning text-dark px-3 py-2 shadow-sm animate__animated animate__pulse animate__infinite";
        badge.innerHTML = '<i class="bi bi-exclamation-triangle-fill me-1"></i> Desincronizado';
    }
    if (warning) warning.style.display = 'block';
    if (initBtn) initBtn.disabled = true;
    if (manualBtn) manualBtn.disabled = true;
    if (applyBtn) {
        applyBtn.disabled = false;
        applyBtn.innerHTML = '<i class="bi bi-cpu"></i> Aplicar al Hardware';
    }
}

export function marcarEmuSincronizado() {
    const badge = document.getElementById('sync-status-emu');
    const warning = document.getElementById('sync-warning-emu');
    const initBtn = document.getElementById('btn-iniciar-emulacion');
    const manualBtn = document.getElementById('btn-enviar-manual-emu');
    const applyBtn = document.getElementById('btn-aplicar-config-emu');

    if (badge) {
        badge.className = "badge bg-success text-white px-3 py-2 shadow-sm animate__animated animate__bounceIn";
        badge.innerHTML = '<i class="bi bi-check-circle-fill me-1"></i> Sincronizado';
    }
    if (warning) warning.style.display = 'none';
    if (initBtn) {
        initBtn.disabled = false;
        initBtn.classList.remove('opacity-50');
    }
    if (manualBtn) manualBtn.disabled = false;
    if (applyBtn) {
        applyBtn.disabled = false;
        applyBtn.innerHTML = '<i class="bi bi-cpu"></i> Aplicar al Hardware';
    }
}

export function conectarDestinoUART() {
    const statusLabel = document.getElementById('status-destino');
    const btnConectar = document.getElementById('btn-conectar-destino');
    const chatArea = document.getElementById('chat-slave-recibido');

    bleDestino.onConnect = () => {
        statusLabel.innerText = "CONECTADO";
        statusLabel.className = "badge bg-primary mb-3 p-2 px-3";
        btnConectar.disabled = true;
        btnConectar.innerHTML = `<i class="bi bi-link"></i> Slave Vinculado`;
        const pingBtn = document.getElementById('btn-ping-destino');
        if (pingBtn) pingBtn.disabled = false;
        alert("✅ Slave Conectado.");
    };

    bleDestino.onDisconnect = () => {
        statusLabel.innerText = "DESCONECTADO";
        statusLabel.className = "badge bg-secondary mb-3 p-2 px-3";
        btnConectar.disabled = false;
        btnConectar.innerHTML = `<i class="bi bi-link-45deg"></i> Conectar Slave`;
        const pingBtn = document.getElementById('btn-ping-destino');
        if (pingBtn) pingBtn.disabled = true;
    };

    bleDestino.onDataReceived = (data) => {
        if (data.includes("PONG_DESTINO")) {
            alert("¡PONG! Pruebas Slave respondiendo correctamente.");
            return;
        }

        if (chatArea) {
            chatArea.value += data + "\n";
            chatArea.scrollTop = chatArea.scrollHeight;
        }
    };

    bleDestino.connect();
}

export function responderDestinoUART() {
    const data = document.getElementById('destino-manual-data').value;
    if (!data) return;
    bleDestino.sendData(data);
    document.getElementById('destino-manual-data').value = "";
}

export function limpiarChat(rol) {
    const id = (rol === 'master') ? 'chat-master-recibido' : 'chat-slave-recibido';
    const area = document.getElementById(id);
    if (area) area.value = "";
}

window.enviarTextoTFT = function () {
    const text = document.getElementById('tft-text-input').value;
    if (!text) return;
    const cmd = `EMU_MSG:SPI_TFT:${text}`; // Formato simplificado: EMU_MSG:PROTOCOL:DATA
    bleEmulador.sendData(cmd);
    console.log(`Enviando Texto a TFT: ${text}`);
}

window.setBrilloTFT = function (val) {
    const cmd = `EMU_MSG:SPI_TFT_BRI:${val}`; // Formato simplificado
    bleEmulador.sendData(cmd);
}

window.limpiarChat = limpiarChat;
window.conectarDestinoUART = conectarDestinoUART;
window.responderDestinoUART = responderDestinoUART;
window.pingDestinoUART = () => bleDestino.sendData("PING");

window.conectarEmulador = conectarEmulador;
window.pingEmulador = () => bleEmulador.sendData("PING");
window.cambiarVistaConfig = cambiarVistaConfig;
window.aplicarEmulacion = aplicarEmulacion;
window.iniciarEmulacion = iniciarEmulacion;
window.detenerEmulacion = detenerEmulacion;
window.enviarMensajeManual = enviarMensajeManual;
window.marcarEmuDesincronizado = marcarEmuDesincronizado;
window.marcarEmuSincronizado = marcarEmuSincronizado;

// Auto-inicializar
document.addEventListener('DOMContentLoaded', () => {
    if (document.getElementById('protocoloEmuSelect')) {
        // Resetear variables globales
        bmpCalib = null;

        // Limpiar UI
        const sensorData = document.getElementById('sensor-data-display');
        if (sensorData) sensorData.style.display = 'none';

        const rawContainer = document.getElementById('raw-data-container');
        if (rawContainer) rawContainer.style.display = 'none';

        cambiarVistaConfig();
    }

    // === EVENT DELEGATION para controles TFT ===
    // El listener se pone en el contenedor padre PERMANENTE (siempre en DOM).
    // Así funciona aunque el innerHTML interno se destruya y recree.
    const fullWidthRow = document.getElementById('emu-full-width-config-row');
    if (fullWidthRow) {
        // Botón de enviar texto
        fullWidthRow.addEventListener('click', (e) => {
            if (e.target.closest('#btn-tft-send')) {
                const input = document.getElementById('tft-text-input');
                if (!input || !input.value.trim()) return;
                const cmd = `EMU_MSG:SPI_TFT:${input.value.trim()}`;
                console.log('[TFT] Enviando:', cmd);
                bleEmulador.sendData(cmd);
                input.value = '';
            }
        });

        // Enter en el input de texto
        fullWidthRow.addEventListener('keydown', (e) => {
            if (e.key === 'Enter' && e.target.id === 'tft-text-input') {
                const cmd = `EMU_MSG:SPI_TFT:${e.target.value.trim()}`;
                if (!e.target.value.trim()) return;
                console.log('[TFT] Enviando (Enter):', cmd);
                bleEmulador.sendData(cmd);
                e.target.value = '';
            }
        });

        // Slider de brillo
        fullWidthRow.addEventListener('input', (e) => {
            if (e.target.id === 'tft-brightness-range') {
                const pct = Math.round(e.target.value / 2.55) + '%';
                const label = document.getElementById('tft-brightness-val');
                if (label) label.innerText = pct;
                const cmd = `EMU_MSG:SPI_TFT_BRI:${e.target.value}`;
                console.log('[TFT] Brillo:', cmd);
                bleEmulador.sendData(cmd);
            }
        });
    }
});
