import { wifiAnalizador } from './wifi_connection.js';

let chartCH1, chartCH2;
// Colas para Transmisión (Salida Manual)
let queueTX = { bits: [], clock: [], lastBit: 1, lastClock: 1 };
// Colas para Recepción Canal 1 (Master Side)
let queueCH1 = { rx: [], tx: [], sck: [], cs: [], lastRX: 1, lastTX: 1, lastSCK: 1, lastCS: 1 };
// Colas para Recepción Canal 2 (Slave Side)
let queueCH2 = { rx: [], tx: [], sck: [], cs: [], lastRX: 1, lastTX: 1, lastSCK: 1, lastCS: 1 };


let MAX_DISPLAY_POINTS = 300;

// --- ESTADO DEL ASISTENTE DE SENSORES ---
let activeWizard = "NONE";
let wizardTimer = null;
let wizardStep = 0;
let bmpCalib = null;
// ----------------------------------------

// ========== FUNCIONES DE ESCALA VISUAL ============
window.changeChartSize = function (size) {
    MAX_DISPLAY_POINTS = parseInt(size);
    resetCharts();
}

/**
 * Levanta las instancias de variables y Canvas de Chart.js
 */
export function initChart() {
    const canvas1 = document.getElementById('chartCH1');
    const canvas2 = document.getElementById('chartCH2');
    if (!canvas1 || !canvas2) return;

    chartCH1 = createOscilloscopeInstance(canvas1, "CH1", "#00f2ff", "#7000ff");
    chartCH2 = createOscilloscopeInstance(canvas2, "CH2", "#ff00ff", "#00ff00");

    // Usar requestAnimationFrame para suavidad máxima (60 FPS aprox)
    function renderLoop() {
        updateOscilloscopes();
        requestAnimationFrame(renderLoop);
    }
    requestAnimationFrame(renderLoop);
}

function createOscilloscopeInstance(canvas, label, color1, color2) {
    const ctx = canvas.getContext('2d');
    const initialData = Array.from({ length: MAX_DISPLAY_POINTS }, () => 1);

    return new Chart(ctx, {
        type: 'line',
        data: {
            labels: Array.from({ length: MAX_DISPLAY_POINTS }, (_, i) => i),
            datasets: [
                {
                    label: label + ' (Input)',
                    data: [...initialData],
                    borderColor: color1,
                    borderWidth: 2,
                    stepped: true,
                    pointRadius: 0,
                    fill: false,
                    tension: 0
                },
                {
                    label: label + ' (Output/Eco)',
                    data: initialData.map((v, i) => v - 0.4),
                    borderColor: color2,
                    borderWidth: 2,
                    stepped: true,
                    pointRadius: 0,
                    fill: false,
                    tension: 0
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            animation: false,
            spanGaps: true,
            scales: {
                x: { display: false },
                y: { min: -1.5, max: 1.5, ticks: { display: false }, grid: { color: 'rgba(0,0,0,0.05)' } }
            },
            plugins: {
                legend: { display: true, labels: { boxWidth: 10, font: { size: 10 } } }
            }
        }
    });
}

function updateOscilloscopes() {
    const isConnected = wifiAnalizador && wifiAnalizador.isConnected;
    const hasData = queueCH1.rx.length > 0 || queueCH2.rx.length > 0 || queueTX.bits.length > 0;

    if (isConnected || hasData) {
        const proto = document.getElementById('protocoloSelect')?.value;
        shiftChart(chartCH1, queueCH1, proto);
        if (proto === 'RS232') {
            shiftChart(chartCH2, queueCH2, proto);
        }
    }
}

function shiftChart(chart, queue, proto) {
    if (!chart || !chart.data.datasets.length) return;
    
    // Lógica adaptativa: si hay muchos datos, procesamos más por frame para no retrasarnos
    let samplesToProcess = 1;
    if (queue.rx.length > MAX_DISPLAY_POINTS * 2) samplesToProcess = 10;
    else if (queue.rx.length > MAX_DISPLAY_POINTS) samplesToProcess = 4;
    else if (queue.rx.length > 50) samplesToProcess = 2;

    for (let s = 0; s < samplesToProcess; s++) {
        let vals = [];
        
        if (proto === 'SPI') {
            vals[0] = (queue.rx.length > 0) ? queue.rx.shift() : queue.lastRX; 
            vals[1] = (queue.tx.length > 0) ? queue.tx.shift() : queue.lastTX; 
            vals[2] = (queue.sck && queue.sck.length > 0) ? queue.sck.shift() : (queue.lastSCK || 1); 
            vals[3] = (queue.cs && queue.cs.length > 0) ? queue.cs.shift() : (queue.lastCS || 1); 
            
            queue.lastRX = vals[0]; queue.lastTX = vals[1]; queue.lastSCK = vals[2]; queue.lastCS = vals[3];
        } else {
            vals[0] = (queue.rx.length > 0) ? queue.rx.shift() : queue.lastRX;
            vals[1] = (queue.tx.length > 0) ? queue.tx.shift() : queue.lastTX;
            queue.lastRX = vals[0]; queue.lastTX = vals[1];
        }

        chart.data.datasets.forEach((dataset, i) => {
            dataset.data.shift();
            let offset = i * 0.4;
            let val = (vals[i] !== undefined) ? vals[i] : 1;
            dataset.data.push(val - offset);
        });
    }
    chart.update('none');

    // Ocultar overlays
    if (queue.rx.length > 0) {
        const id = chart === chartCH1 ? 'overlay-ch1' : 'overlay-ch2';
        document.getElementById(id)?.classList.add('hidden');
    }
}

export function resetCharts() {
    queueCH1 = { rx: [], tx: [], sck: [], cs: [], lastRX: 1, lastTX: 1, lastSCK: 1, lastCS: 1 };
    queueCH2 = { rx: [], tx: [], sck: [], cs: [], lastRX: 1, lastTX: 1, lastSCK: 1, lastCS: 1 };
    queueTX = { bits: [], clock: [], lastBit: 1, lastClock: 1 };
    
    [chartCH1, chartCH2].forEach(chart => {
        if (!chart) return;
        
        // CORRECCIÓN: Actualizar etiquetas para que coincidan con el nuevo tamaño
        chart.data.labels = Array.from({ length: MAX_DISPLAY_POINTS }, (_, i) => i);
        
        chart.data.datasets.forEach((dataset, i) => {
            dataset.data = Array(MAX_DISPLAY_POINTS).fill(1 - (i * 0.4));
        });
        chart.update('none');
    });
    document.getElementById('overlay-ch1')?.classList.remove('hidden');
    document.getElementById('overlay-ch2')?.classList.remove('hidden');
}

// =========================================================
// 2. PROCESAMIENTO DE PROTOCOLOS
// =========================================================

function generarBits(protocolo, bytes) {
    let rx = [];
    let tx = [];
    let sck = [];
    let cs = [];

    if (protocolo === 'I2C' || protocolo === 'SPI') {
        bytes.forEach(b => {
            let val = parseInt(b, 16);
            if (protocolo === 'SPI') cs.push(1, 1, 1); // Idle high
            
            for (let i = 7; i >= 0; i--) {
                let bit = (val >> i) & 1;
                rx.push(bit, bit);
                tx.push(bit, bit); // Simular MISO igual a MOSI si no hay buffer separado
                sck.push(0, 1);
                if (protocolo === 'SPI') cs.push(0, 0); // Active low
            }
            if (protocolo === 'I2C') { rx.push(0, 0); tx.push(0, 0); sck.push(0, 1); }
            if (protocolo === 'SPI') cs.push(1, 1, 1); // Return high
        });
    } else {
        bytes.forEach(b => {
            let val = parseInt(b, 16);
            for (let i = 0; i < 3; i++) { rx.push(0); tx.push(1); } // Start bit
            for (let i = 0; i < 8; i++) {
                let bit = (val >> i) & 1;
                for (let j = 0; j < 3; j++) { rx.push(bit); tx.push(1); }
            }
            for (let i = 0; i < 3; i++) { rx.push(1); tx.push(1); } // Stop bit
        });
    }
    return { rx, tx, sck, cs };
}

function getFormattedTime() {
    const now = new Date();
    return now.toLocaleTimeString('es-MX', { hour12: false }) + '.' + String(now.getMilliseconds()).padStart(3, '0');
}

export function startLiveClock() {
    const clockEl = document.getElementById('reloj-digital');
    if (!clockEl) return;
    setInterval(() => { clockEl.innerText = getFormattedTime(); }, 50);
}

export function procesarEntradaAnalizador(linea) {

    if (linea.includes("PONG")) { alert("¡Conexión verificada!"); return; }
    if (linea.includes("CONFIG_OK")) {
        alert("✅ Configuración aplicada con éxito.");
        marcarProxySincronizado();
        return;
    }

    const time = getFormattedTime();
    const parts = linea.split(':');

    if (linea.includes("_RECV_BUF")) {
        let isError = linea.includes("_ERR:");
        if (isError) { alert("❌ Error de transmisión: " + parts[1]); return; }

        let esProxy = linea.includes(":M2S:") || linea.includes(":S2M:");
        let hexList = esProxy ? parts[2].split(',') : parts[1].split(',');
        hexList = hexList.filter(x => x.trim() !== "");

        const protoStr = parts[0].split('_')[0];

        // Especial para SPI: Manejar pares MOSI, MISO
        if (protoStr === "SPI") {
            for (let i = 0; i < hexList.length; i += 2) {
                const mosiHex = hexList[i];
                const misoHex = hexList[i + 1] || "00";

                const mosiClean = mosiHex.toUpperCase().startsWith('0X') ? mosiHex.toUpperCase() : "0x" + mosiHex.toUpperCase();
                const misoClean = misoHex.toUpperCase().startsWith('0X') ? misoHex.toUpperCase() : "0x" + misoHex.toUpperCase();

                // 1. Gráfica Sincronizada
                const mosiSignals = generarBits("SPI", [mosiClean]);
                const misoSignals = generarBits("SPI", [misoClean]);

                queueCH1.rx.push(...mosiSignals.rx);  // MOSI
                queueCH1.tx.push(...misoSignals.rx);  // MISO (rx del generador de bits de miso)
                queueCH1.sck.push(...mosiSignals.sck); // SCK
                queueCH1.cs.push(...mosiSignals.cs);   // CS

                // 2. Tabla (Colores solicitados: MOSI: Azul, MISO: Blanco, CS: Naranja, SCK: Gris)
                agregarFilaTabla(time, "CS", "SPI", "Active LOW", "bg-cs-orange");
                agregarFilaTabla(time, "SCK", "SPI", "Clock Pulse", "bg-sck-gray");
                agregarFilaTabla(time, "MOSI", "SPI", mosiClean, "bg-mosi-blue");
                agregarFilaTabla(time, "MISO", "SPI", misoClean, "bg-miso-white");
            }
        } else {
            // RS232 e I2C (procesamiento original)
            hexList.forEach(hex => {
                const cleanHex = hex.toUpperCase().startsWith('0X') ? hex.toUpperCase() : "0x" + hex.toUpperCase();
                let esM2S = linea.includes(":M2S:");
                
                let direccion = "RX";
                let colorBadge = "bg-secondary";

                if (protoStr === "RS232") {
                    direccion = esProxy ? (esM2S ? "MSTR ➔ SLV" : "SLV ➔ MSTR") : "UART";
                    colorBadge = esM2S ? "bg-info" : "bg-danger";
                } else if (protoStr === "I2C") {
                    direccion = "SDA";
                    colorBadge = "bg-primary";
                }

                // 1. Gráfica
                const signals = generarBits(protoStr === "RS232" ? "RS232" : protoStr, [cleanHex]);
                if (protoStr === "RS232") {
                    if (esM2S) {
                        queueCH1.rx.push(...signals.rx);
                        queueCH2.tx.push(...signals.rx);
                    } else {
                        queueCH2.rx.push(...signals.rx);
                        queueCH1.tx.push(...signals.rx);
                    }
                } else {
                    queueCH1.rx.push(...signals.rx);
                    queueCH1.tx.push(...signals.sck);
                }

                // 2. Tabla
                agregarFilaTabla(time, direccion, protoStr, cleanHex, colorBadge);
            });
        }

        // Auto-Sincronización
        if (document.getElementById('btn-aplicar-config-proxy')?.innerHTML.includes('Sync')) {
            marcarProxySincronizado();
        }

        // Wizard Intercepción
        if (activeWizard !== "NONE") procesarWizard(parts[0], hexList);
    }
}

function agregarFilaTabla(time, direccion, proto, dato, badgeClass) {
    const body = document.getElementById('decodificacionBody');
    if (body) {
        if (body.innerHTML.includes('Esperando actividad')) body.innerHTML = '';
        const row = `<tr>
            <td class="font-monospace text-muted small">${time}</td>
            <td><span class="badge ${badgeClass}">${direccion}</span></td>
            <td>${proto}</td>
            <td class="fw-bold font-monospace">${dato}</td>
            <td class="text-success small">OK</td>
        </tr>`;
        body.insertAdjacentHTML('afterbegin', row);
    }
}

function procesarWizard(header, hexList) {

    if (activeWizard === "MAX6675" && header === "SPI_RECV_BUF") {
        const val = (parseInt(hexList[0], 16) << 8) | parseInt(hexList[1], 16);
        const temp = ((val >> 3) * 0.25).toFixed(2);
        updateSensorDisplay(`<div class="text-center"><i class="bi bi-thermometer-half text-danger display-6"></i><div class="h2 fw-bold text-dark mb-0">${temp} °C</div></div>`);
    }
}

export function actualizarConfiguracion(selectId) {
    const proto = document.getElementById(selectId).value;
    const configDiv = document.getElementById('configuracionDinamica');

    // Elementos de la UI
    const containerCH2 = document.getElementById('container-ch2');
    const badgeCH1 = document.getElementById('badge-ch1');
    const overlayTextCH1 = document.getElementById('overlay-text-ch1');

    if (chartCH1 && chartCH2) {
        if (proto === 'RS232') {
            // Mostrar ambos canales
            if (containerCH2) containerCH2.style.display = 'block';
            badgeCH1.innerText = "CANAL 1 (Master Side)";
            badgeCH1.className = "badge bg-info";
            overlayTextCH1.innerText = "SIN SEÑAL CH1";

            // Reconfigurar Chart 1 (2 datasets)
            reconfigurarDatasets(chartCH1, [
                { label: "RX1 (Input Maestro)", color: "#00f2ff" },
                { label: "TX1 (Output Maestro)", color: "#7000ff" }
            ]);

            // Reconfigurar Chart 2 (2 datasets)
            reconfigurarDatasets(chartCH2, [
                { label: "RX2 (Input Esclavo)", color: "#ff00ff" },
                { label: "TX2 (Output Esclavo)", color: "#00ff00" }
            ]);
        } else if (proto === 'I2C') {
            // Ocultar canal 2
            if (containerCH2) containerCH2.style.display = 'none';
            badgeCH1.innerText = "BUS I²C (SDA / SCL)";
            badgeCH1.className = "badge bg-primary";
            overlayTextCH1.innerText = "SIN SEÑAL I²C";

            // Reconfigurar Chart 1 (2 datasets)
            reconfigurarDatasets(chartCH1, [
                { label: "SDA (Datos)", color: "#22c55e" },
                { label: "SCL (Reloj)", color: "#eab308" }
            ]);
        } else if (proto === 'SPI') {
            // Ocultar canal 2
            if (containerCH2) containerCH2.style.display = 'none';
            badgeCH1.innerText = "BUS SPI (MOSI / MISO / SCK / CS)";
            badgeCH1.className = "badge bg-warning text-dark";
            overlayTextCH1.innerText = "SIN SEÑAL SPI";

            // Reconfigurar Chart 1 (4 datasets)
            reconfigurarDatasets(chartCH1, [
                { label: "MOSI", color: "#3b82f6" },
                { label: "MISO", color: "#6366f1" },
                { label: "SCK", color: "#f59e0b" },
                { label: "CS", color: "#ef4444" }
            ]);
        }
    }

    function reconfigurarDatasets(chart, configs) {
        chart.data.datasets = configs.map((c, i) => ({
            label: c.label,
            data: Array(MAX_DISPLAY_POINTS).fill(1 - (i * 0.4)), 
            borderColor: c.color,
            borderWidth: 2,
            stepped: true,
            pointRadius: 0,
            fill: false,
            tension: 0
        }));
        chart.update(); // Actualización completa para re-inicializar metadatos
    }

    if (proto === 'I2C') {
        configDiv.innerHTML = `<div class="text-center p-3"><i class="bi bi-diagram-2 text-primary" style="font-size:2rem;"></i><div class="fw-bold text-primary mt-2">Escucha Pasiva I²C</div></div>`;
    } else if (proto === 'SPI') {
        configDiv.innerHTML = `<div class="text-center p-3"><i class="bi bi-usb-symbol text-warning" style="font-size:2rem;"></i><div class="fw-bold text-warning mt-2">Escucha Pasiva SPI</div></div>`;
    } else if (proto === 'RS232') {
        configDiv.innerHTML = `<div class="row g-2 mt-1"><div class="col-md-12 text-start"><label class="small fw-bold text-muted">Baud Rate</label>
            <select id="baudSelect" class="form-select form-select-sm border-0 bg-light" onchange="marcarProxyDesincronizado()">
                <option value="9600">9600 bps</option>
                <option value="19200">19200 bps</option>
                <option value="38400">38400 bps</option>
                <option value="57600">57600 bps</option>
                <option value="115200" selected>115200 bps</option>
            </select></div></div>`;
    }
    marcarProxyDesincronizado();
}

// =========================================================
// 3. ASISTENTE DE SENSORES (WIZARD)
// =========================================================

window.toggleSensorWizard = function () {
    const select = document.getElementById('sensorWizardSelect');
    const btn = document.getElementById('btn-start-wizard');
    const display = document.getElementById('sensor-display-area');

    if (activeWizard === "NONE") {
        if (select.value === "NONE") { alert("Por favor selecciona un sensor primero."); return; }
        activeWizard = select.value;
        btn.innerHTML = '<i class="bi bi-stop-fill"></i> Detener Monitor';
        btn.className = "btn btn-danger btn-sm w-100 fw-bold shadow-sm";
        select.disabled = true;
        display.innerHTML = '<div class="spinner-border text-info"></div>';
        wizardTimer = setInterval(tickSensorWizard, 1000);
    } else {
        clearInterval(wizardTimer);
        activeWizard = "NONE";
        btn.innerHTML = '<i class="bi bi-play-fill"></i> Iniciar Monitor';
        btn.className = "btn btn-info btn-sm text-white w-100 fw-bold shadow-sm";
        select.disabled = false;
        display.innerHTML = '<div class="small mt-1 text-muted">Selecciona un sensor arriba.</div>';
    }
}

function tickSensorWizard() {
    if (!wifiAnalizador || !wifiAnalizador.isConnected) return;
    if (activeWizard === "MAX6675") wifiAnalizador.sendData("SPI:00,00");
}

function updateSensorDisplay(html) {
    const display = document.getElementById('sensor-display-area');
    if (display) display.innerHTML = html;
}

export function clearData() {
    const body = document.getElementById('decodificacionBody');
    if (body) {
        body.innerHTML = `<tr><td colspan="5" class="text-center text-muted py-5"><div style="font-size: 2rem; opacity: 0.2;"><i class="bi bi-inbox"></i></div>Esperando actividad en el bus...</td></tr>`;
    }
}

export function enviarConfiguracionFisica() {
    if (!wifiAnalizador || !wifiAnalizador.isConnected) return;
    const proto = document.getElementById('protocoloSelect').value;
    let cmd = "CONFIG:" + proto + ":";
    if (proto === 'RS232') cmd += document.getElementById('baudSelect')?.value || "115200";
    else if (proto === 'I2C') cmd += "100000";
    else if (proto === 'SPI') cmd += "0";

    const applyBtn = document.getElementById('btn-aplicar-config-proxy');
    if (applyBtn) {
        applyBtn.disabled = true;
        applyBtn.innerHTML = '<span class="spinner-border spinner-border-sm"></span> Sync...';
    }
    wifiAnalizador.sendData(cmd);
}

export function marcarProxyDesincronizado() {
    const badge = document.getElementById('sync-status-proxy');
    const applyBtn = document.getElementById('btn-aplicar-config-proxy');
    if (badge) {
        badge.className = "badge bg-warning text-dark px-3 py-2";
        badge.innerHTML = 'Desincronizado';
    }
    if (applyBtn) {
        applyBtn.disabled = false;
        applyBtn.innerHTML = '<i class="bi bi-cpu"></i> Aplicar al Hardware';
    }
}

export function marcarProxySincronizado() {
    const badge = document.getElementById('sync-status-proxy');
    const applyBtn = document.getElementById('btn-aplicar-config-proxy');
    if (badge) {
        badge.className = "badge bg-success text-white px-3 py-2";
        badge.innerHTML = 'Sincronizado';
    }
    if (applyBtn) {
        applyBtn.disabled = true;
        applyBtn.innerHTML = 'Aplicado';
    }
}

window.marcarProxyDesincronizado = marcarProxyDesincronizado;
window.marcarProxySincronizado = marcarProxySincronizado;
window.resetCharts = resetCharts;
window.clearData = clearData;
window.enviarConfiguracionFisica = enviarConfiguracionFisica;
window.actualizarConfiguracion = actualizarConfiguracion;
window.probarPing = () => {
    if (wifiAnalizador && wifiAnalizador.isConnected) {
        wifiAnalizador.sendData("PING");
    } else {
        alert("⚠️ El visualizador no está conectado.");
    }
};


window.conectarAnalizador = () => {
    const statusEl = document.getElementById('status-analizador');
    const connectBtn = document.getElementById('btn-conectar-analizador');

    wifiAnalizador.onConnect = () => {
        if (statusEl) {
            statusEl.innerText = "CONECTADO";
            statusEl.className = "badge bg-purple mb-3 p-2 px-3 text-white";
        }
        if (connectBtn) {
            connectBtn.disabled = true;
            connectBtn.innerHTML = `Visualizador Vinculado`;
        }
        const pingBtn = document.getElementById('btn-ping');
        if (pingBtn) pingBtn.disabled = false;

        actualizarConfiguracion('protocoloSelect');
        alert("✅ Conectado con éxito.");
    };

    wifiAnalizador.onDisconnect = () => {
        if (statusEl) {
            statusEl.innerText = "DESCONECTADO";
            statusEl.className = "badge bg-secondary mb-3 p-2 px-3";
        }
        if (connectBtn) {
            connectBtn.disabled = false;
            connectBtn.innerHTML = `<i class="bi bi-link-45deg"></i> Vincular vía WiFi`;
        }
        const pingBtn = document.getElementById('btn-ping');
        if (pingBtn) pingBtn.disabled = true;
    };



    wifiAnalizador.onDataReceived = procesarEntradaAnalizador;
    const ipInput = document.getElementById('proxy-ip-addr');
    wifiAnalizador.connect(ipInput ? ipInput.value : "192.168.4.1");
};

document.addEventListener('DOMContentLoaded', () => {
    if (document.getElementById('chartCH1')) {
        initChart();
        startLiveClock();
        actualizarConfiguracion('protocoloSelect');
    }
});
