import { wifiAnalizador } from './wifi_connection.js';

let chartCH1, chartCH2;
// Colas para Transmisión (Salida Manual)
let queueTX = { bits: [], clock: [], lastBit: 1, lastClock: 1 };
// Colas para Recepción Canal 1
let queueCH1 = { rx: [], tx: [], sck: [], cs: [], lastRX: 1, lastTX: 1, lastSCK: 1, lastCS: 1 };
// Colas para Recepción Canal 2
let queueCH2 = { rx: [], tx: [], sck: [], cs: [], lastRX: 1, lastTX: 1, lastSCK: 1, lastCS: 1 };


// --- ESTADO DEL ASISTENTE DE SENSORES ---
let activeWizard = "NONE";
let wizardTimer = null;
let wizardStep = 0;
let bmpCalib = null;
// ----------------------------------------
let MAX_DISPLAY_POINTS = 300;
let MAX_HISTORY_POINTS = 5000; // Buffer total de historial
let isPaused = false;
let historyOffset = 0; // Desplazamiento desde el final (0 = ahora)

// Buffers de historial
let historyCH1 = { rx: [], tx: [], sck: [], cs: [] };
let historyCH2 = { rx: [], tx: [], sck: [], cs: [] };

// ========== FUNCIONES DE ESCALA VISUAL ============
window.changeChartSize = function (size) {
    MAX_DISPLAY_POINTS = parseInt(size);

    [chartCH1, chartCH2].forEach(chart => {
        if (!chart) return;
        // Actualizar el eje X para reflejar la nueva cantidad de puntos
        chart.data.labels = Array.from({ length: MAX_DISPLAY_POINTS }, (_, i) => i);
    });

    if (isPaused) {
        updateHistorySlider();
    } else {
        historyOffset = 0;
    }

    // Re-renderizar basándose en el historial acumulado
    renderHistoricalData(chartCH1, historyCH1);
    const proto = document.getElementById('protocoloSelect')?.value;
    if (proto === 'UART') {
        renderHistoricalData(chartCH2, historyCH2);
    }
}

/**
 * Levanta las instancias de variables y Canvas de Chart.js
 */
export function initChart() {
    const canvas1 = document.getElementById('chartCH1');
    const canvas2 = document.getElementById('chartCH2');
    if (!canvas1 || !canvas2) return;

    // IMPORTANTE: Destruir instancias previas si existen para evitar el error "Canvas is already in use"
    if (chartCH1) chartCH1.destroy();
    if (chartCH2) chartCH2.destroy();

    chartCH1 = createOscilloscopeInstance(canvas1, "CH1", "#00f2ff", "#7000ff");
    chartCH2 = createOscilloscopeInstance(canvas2, "CH2", "#ff00ff", "#00ff00");

    // Usar requestAnimationFrame para suavidad máxima
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
                    label: 'SDA / MOSI / RX',
                    data: [...initialData],
                    borderColor: '#00ff00', // Verde
                    borderWidth: 2,
                    stepped: true,
                    pointRadius: 0,
                    fill: false,
                    tension: 0
                },
                {
                    label: 'SCL / MISO / TX',
                    data: initialData.map((v, i) => v - 0.5),
                    borderColor: '#ffaa00', // Naranja/Amarillo
                    borderWidth: 2,
                    stepped: true,
                    pointRadius: 0,
                    fill: false,
                    tension: 0
                },
                {
                    label: 'SCK / MARCA',
                    data: initialData.map((v, i) => v - 1.0),
                    borderColor: '#00ccff', // Azul claro
                    borderWidth: 1.5,
                    stepped: true,
                    pointRadius: 0,
                    fill: false,
                    tension: 0
                },
                {
                    label: 'CS / EVENTO',
                    data: initialData.map((v, i) => v - 1.5),
                    borderColor: '#ff00ff', // Magenta
                    borderWidth: 1.5,
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
                y: { min: -2.0, max: 1.5, ticks: { display: false }, grid: { color: 'rgba(0,0,0,0.1)' } }
            },
            plugins: {
                legend: {
                    display: true,
                    position: 'top',
                    labels: {
                        color: '#000',
                        boxWidth: 10,
                        font: { size: 10 },
                        // Ocultar de la leyenda si no tiene texto (para canales no usados)
                        filter: (item) => item.text !== ""
                    }
                }
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
        if (proto === 'UART') {
            shiftChart(chartCH2, queueCH2, proto);
        }
    }
}

function shiftChart(chart, queue, proto) {
    if (!chart || !chart.data.datasets.length) return;

    // Guardar en historial siempre
    const hist = chart === chartCH1 ? historyCH1 : historyCH2;

    // Lógica adaptativa: si hay muchos datos, procesamos más por frame para no retrasarnos
    let samplesToProcess = 1;
    if (queue.rx.length > MAX_DISPLAY_POINTS * 2) samplesToProcess = 15;
    else if (queue.rx.length > MAX_DISPLAY_POINTS) samplesToProcess = 5;
    else if (queue.rx.length > 50) samplesToProcess = 2;

    for (let s = 0; s < samplesToProcess; s++) {
        let vals = [];

        if (proto === 'SPI') {
            vals[0] = (queue.rx.length > 0) ? queue.rx.shift() : queue.lastRX;
            vals[1] = (queue.tx.length > 0) ? queue.tx.shift() : queue.lastTX;
            vals[2] = (queue.sck && queue.sck.length > 0) ? queue.sck.shift() : (queue.lastSCK || 1);
            vals[3] = (queue.cs && queue.cs.length > 0) ? queue.cs.shift() : (queue.lastCS || 1);

            queue.lastRX = vals[0]; queue.lastTX = vals[1]; queue.lastSCK = vals[2]; queue.lastCS = vals[3];

            // Push a historial
            hist.rx.push(vals[0]); hist.tx.push(vals[1]);
            hist.sck.push(vals[2]); hist.cs.push(vals[3]);
        } else {
            vals[0] = (queue.rx.length > 0) ? queue.rx.shift() : queue.lastRX;
            vals[1] = (queue.tx.length > 0) ? queue.tx.shift() : queue.lastTX;
            queue.lastRX = vals[0]; queue.lastTX = vals[1];

            // Push a historial
            hist.rx.push(vals[0]); hist.tx.push(vals[1]);
        }

        // Limitar historial
        if (hist.rx.length > MAX_HISTORY_POINTS) {
            hist.rx.shift(); hist.tx.shift();
            if (hist.sck) { hist.sck.shift(); hist.cs.shift(); }
        }

        // Solo actualizar datos del chart si NO está pausado
        if (!isPaused) {
            chart.data.datasets.forEach((dataset, i) => {
                dataset.data.shift();
                let offset = i * 0.5; // Ajustado para 4 datasets
                let val = (vals[i] !== undefined) ? vals[i] : 1;
                dataset.data.push(val - offset);
            });
        }
    }

    if (!isPaused) {
        chart.update('none');
    }

    // Ocultar overlays si hay cualquier actividad (RX o TX)
    if (queue.rx.length > 0 || queue.tx.length > 0) {
        document.getElementById('overlay-ch1')?.classList.add('hidden');
        document.getElementById('overlay-ch2')?.classList.add('hidden');
    }
}

window.togglePause = function () {
    isPaused = !isPaused;
    const btn = document.getElementById('btn-pause-chart');
    const nav = document.getElementById('history-navigator');

    if (isPaused) {
        btn.innerHTML = '<i class="bi bi-play-fill"></i> Reanudar Captura';
        btn.classList.replace('btn-purple', 'btn-success');
        nav.style.display = 'block';
        updateHistorySlider();
    } else {
        btn.innerHTML = '<i class="bi bi-pause-fill"></i> Pausar Captura';
        btn.classList.replace('btn-success', 'btn-purple');
        nav.style.display = 'none';
        historyOffset = 0;
        // Al reanudar, el chart se sincroniza solo en el siguiente frame
    }
}

function updateHistorySlider() {
    const range = document.getElementById('chartHistoryRange');
    const max = Math.max(0, historyCH1.rx.length - MAX_DISPLAY_POINTS);
    range.max = max;
    range.value = max;
    document.getElementById('history-offset-label').innerText = "Ahora";
}

window.navigateHistory = function (val) {
    if (!isPaused) return;
    const max = parseInt(document.getElementById('chartHistoryRange').max);
    historyOffset = max - parseInt(val);

    document.getElementById('history-offset-label').innerText = historyOffset === 0 ? "Ahora" : `-${historyOffset} pts`;

    // Forzar redibujado de charts con datos históricos
    renderHistoricalData(chartCH1, historyCH1);
    if (document.getElementById('protocoloSelect').value === 'UART') {
        renderHistoricalData(chartCH2, historyCH2);
    }
}

function renderHistoricalData(chart, history) {
    if (!chart || history.rx.length < MAX_DISPLAY_POINTS) return;

    const end = history.rx.length - historyOffset;
    const start = Math.max(0, end - MAX_DISPLAY_POINTS);

    chart.data.datasets.forEach((dataset, i) => {
        let source;
        if (i === 0) source = history.rx;
        else if (i === 1) source = history.tx;
        else if (i === 2) source = history.sck || [];
        else if (i === 3) source = history.cs || [];

        const slice = source.slice(start, end);
        // Rellenar si el slice es corto (Idle = 1)
        while (slice.length < MAX_DISPLAY_POINTS) slice.unshift(1);

        dataset.data = slice.map(v => v - (i * 0.5)); // Ajustado para 4 datasets
    });
    chart.update('none');
}

export function resetCharts() {
    queueCH1 = { rx: [], tx: [], sck: [], cs: [], lastRX: 1, lastTX: 1, lastSCK: 1, lastCS: 1 };
    queueCH2 = { rx: [], tx: [], sck: [], cs: [], lastRX: 1, lastTX: 1, lastSCK: 1, lastCS: 1 };
    queueTX = { bits: [], clock: [], lastBit: 1, lastClock: 1 };

    // Resetear historiales
    historyCH1 = { rx: [], tx: [], sck: [], cs: [] };
    historyCH2 = { rx: [], tx: [], sck: [], cs: [] };
    historyOffset = 0;
    if (isPaused) togglePause(); // Quitar pausa si estaba puesta

    [chartCH1, chartCH2].forEach(chart => {
        if (!chart) return;

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

    if (protocolo === 'I2C') {
        // --- CONDICIÓN DE INICIO (START) ---
        // Marcamos con un pulso en el canal de EVENTO
        rx.push(1, 1, 0, 0);   // SDA cae
        tx.push(1, 1, 1, 1);   // SCL sigue alta
        sck.push(0, 0, 0, 0);  // Sin marca
        cs.push(0, 1, 1, 0);   // ¡PULSO DE START!

        bytes.forEach(b => {
            let val = parseInt(b, 16);
            for (let i = 7; i >= 0; i--) {
                let bit = (val >> i) & 1;
                rx.push(bit, bit);
                tx.push(1, 1);
                sck.push(0, 1);
                cs.push(0, 0);
            }
            // ACK bit (simulado)
            rx.push(0, 0);
            tx.push(1, 1);
            sck.push(0, 1);
            cs.push(0, 0);
        });

        // --- CONDICIÓN DE PARADA (STOP) ---
        rx.push(0, 0, 1, 1);   // SDA sube
        tx.push(1, 1, 1, 1);   // SCL ya está alta
        sck.push(0, 0, 0, 0);
        cs.push(0, 1, 1, 0);   // ¡PULSO DE STOP!

        // Espacio de separación entre paquetes
        for (let i = 0; i < 6; i++) { rx.push(1); tx.push(1); sck.push(1); cs.push(1); }
    } else if (protocolo === 'SPI') {
        bytes.forEach(b => {
            let val = parseInt(b, 16);
            cs.push(1, 1); // Idle

            for (let i = 7; i >= 0; i--) {
                let bit = (val >> i) & 1;
                rx.push(bit, bit);
                tx.push(bit, bit);
                sck.push(0, 1);
                cs.push(0, 0);
            }
            cs.push(1, 1); // Release
        });
        // Separación
        for (let i = 0; i < 4; i++) { rx.push(1); tx.push(1); sck.push(1); cs.push(1); }
    } else {
        // UART ... (se mantiene igual)
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
    if (linea.startsWith("READY:")) {
        marcarProxySincronizado();
        return;
    }
    if (linea.includes("I2C_TX_OK")) { alert("✅ I2C: Mensaje enviado."); return; }
    if (linea.includes("SPI_TX_OK")) { alert("✅ SPI: Mensaje enviado."); return; }


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

                // 2. Tabla (Solo MOSI y MISO, según solicitud del usuario)
                agregarFilaTabla(time, "MOSI", "SPI", mosiClean, "bg-primary");
                agregarFilaTabla(time, "MISO", "SPI", misoClean, "bg-light text-dark border");
            }
        } else {
            // UART e I2C (procesamiento original)
            hexList.forEach((hex, i) => {
                const cleanHex = hex.toUpperCase().startsWith('0X') ? hex.toUpperCase() : "0x" + hex.toUpperCase();
                let esM2S = linea.includes(":M2S:");

                let direccion = "RX";
                let colorBadge = "bg-secondary";

                if (protoStr === "UART") {
                    // En UART, i determina el lado del puente (0 para M2S, 1 para S2M si vienen en par)
                    // O se usa esM2S si vienen individuales
                    direccion = esM2S ? "Canal 1 > Canal 2" : "Canal 2 > Canal 1";
                    colorBadge = esM2S ? "bg-danger" : "bg-primary"; // Rojo para Canal 1, Azul para Canal 2
                } else if (protoStr === "I2C") {
                    direccion = "SDA";
                    colorBadge = "bg-success"; // Verde para SDA (Datos)
                }

                // 1. Gráfica
                const signals = generarBits(protoStr === "UART" ? "UART" : protoStr, [cleanHex]);
                if (protoStr === "UART") {
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

        // Wizard Intercepción
        if (activeWizard !== "NONE") procesarWizard(parts[0], hexList);
    }
}

function agregarFilaTabla(time, direccion, proto, dato, badgeClass) {
    if (isPaused) return; // No agregar a la tabla si la captura está pausada

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

        // OPTIMIZACIÓN: Limitar el número de filas en el DOM (Mejora de salud 1)
        if (body.rows.length > 100) {
            body.deleteRow(body.rows.length - 1);
        }
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
    const rawVal = document.getElementById(selectId).value;
    const proto = rawVal.split('_')[0];
    const role = rawVal.split('_')[1] || 'SNIFFER';

    const configDiv = document.getElementById('configuracionDinamica');
    const badgeCH1 = document.getElementById('badge-ch1');
    const containerCH2 = document.getElementById('container-ch2');
    const overlayTextCH1 = document.getElementById('overlay-text-ch1');
    const activeControls = document.getElementById('active-modes-controls');

    // 1. Manejo de Controles de Datos (Master/Slave)
    if (activeControls) {
        activeControls.style.display = (role === 'SNIFFER' || proto === 'UART' || (proto === 'I2C' && role === 'SLAVE')) ? 'none' : 'block';
        document.getElementById('slave-memory-config').style.display = (role === 'SLAVE' && proto === 'SPI') ? 'block' : 'none';
        document.getElementById('master-send-config').style.display = (role === 'MASTER') ? 'block' : 'none';
    }

    // 2. Configuración Específica por Protocolo
    if (proto === 'UART') {
        // UART: SIEMPRE mostrar selector de Baud Rate
        configDiv.innerHTML = `
            <div class="row g-2 mt-1">
                <div class="col-md-12 text-start">
                    <label class="small fw-bold text-muted"><i class="bi bi-speedometer2"></i> Velocidad UART (Baud Rate)</label>
                    <select id="baudSelect" class="form-select form-select-sm border-2" onchange="marcarProxyDesincronizado()">
                        <option value="9600">9600 bps</option>
                        <option value="19200">19200 bps</option>
                        <option value="38400">38400 bps</option>
                        <option value="57600">57600 bps</option>
                        <option value="115200" selected>115200 bps</option>
                    </select>
                </div>
            </div>`;

        if (containerCH2) containerCH2.style.display = 'block';
        badgeCH1.innerText = "CANAL 1 ";
        badgeCH1.className = "badge bg-danger"; // Rojo como el LED físico
        overlayTextCH1.innerText = "SIN SEÑAL CH1";

        if (chartCH1 && chartCH2) {
            reconfigurarDatasets(chartCH1, [{ label: "RX1", color: "#a333ff" }, { label: "TX1", color: "#8b4513" }]);
            reconfigurarDatasets(chartCH2, [{ label: "RX2", color: "#a333ff" }, { label: "TX2", color: "#8b4513" }]);
        }

    } else if (proto === 'I2C') {
        const isSniffer = (role === 'SNIFFER');

        if (isSniffer) {
            configDiv.innerHTML = `<div class="text-center p-3 text-warning opacity-75">
                <i class="bi bi-diagram-2" style="font-size:2rem;"></i>
                <div class="fw-bold mt-2 text-uppercase small">I²C Sniffing</div>
            </div>`;
        } else {
            configDiv.innerHTML = `
                <div class="row g-3 py-2">
                    <div class="col-6">
                        <label class="small fw-bold text-muted mb-1">Dirección (HEX)</label>
                        <input type="text" id="i2cAddr" class="form-control form-control-sm bg-light border-0" value="0x08" oninput="marcarProxyDesincronizado()">
                    </div>
                    <div class="col-6">
                        <label class="small fw-bold text-muted mb-1">Registro (HEX)</label>
                        <input type="text" id="i2cReg" class="form-control form-control-sm bg-light border-0" value="0x00" oninput="marcarProxyDesincronizado()">
                    </div>
                    <div class="col-12">
                        <label class="small fw-bold text-muted mb-1">Velocidad de Bus</label>
                        <select id="i2cSpeed" class="form-select form-select-sm bg-light border-0" onchange="marcarProxyDesincronizado()">
                            <option value="100000">Standard Mode (100 kHz)</option>
                            <option value="400000">Fast Mode (400 kHz)</option>
                        </select>
                    </div>
                </div>`;
        }

        if (containerCH2) containerCH2.style.display = 'none';
        badgeCH1.innerText = `BUS I²C (${role})`;
        badgeCH1.className = "badge bg-warning text-dark"; // Amarillo como el LED físico
        overlayTextCH1.innerText = `SIN SEÑAL I²C ${role}`;

        if (chartCH1) {
            reconfigurarDatasets(chartCH1, [{ label: "SDA (Datos)", color: "#00ff00" }, { label: "SCL (Reloj)", color: "#ffff00" }]);
        }

    } else if (proto === 'SPI') {
        const isSniffer = (role === 'SNIFFER');

        if (isSniffer) {
            configDiv.innerHTML = `<div class="text-center p-3 text-primary opacity-75">
                <i class="bi bi-usb-symbol" style="font-size:2rem;"></i>
                <div class="fw-bold mt-2 text-uppercase small">SPI Sniffing</div>
            </div>`;
        } else {
            configDiv.innerHTML = `
                <div class="row g-3 py-2">
                    <div class="col-12">
                        <label class="small fw-bold text-muted mb-1">Modo (CPOL/CPHA)</label>
                        <select id="spiMode" class="form-select form-select-sm bg-light border-0" onchange="marcarProxyDesincronizado()">
                            <option value="0">Modo 0 (0,0)</option>
                            <option value="1">Modo 1 (0,1)</option>
                            <option value="2">Modo 2 (1,0)</option>
                            <option value="3">Modo 3 (1,1)</option>
                        </select>
                    </div>
                    <div class="col-12">
                        <label class="small fw-bold text-muted mb-1">Frecuencia de Reloj</label>
                        <select id="spiFreq" class="form-select form-select-sm bg-light border-0" onchange="marcarProxyDesincronizado()">
                            <option value="1000000">1 MHz</option>
                            <option value="4000000">4 MHz</option>
                            <option value="8000000">8 MHz</option>
                            <option value="16000000">16 MHz</option>
                            <option value="20000000" selected>20 MHz</option>
                        </select>
                    </div>
                </div>`;
        }

        if (containerCH2) containerCH2.style.display = 'none';
        badgeCH1.innerText = `BUS SPI (${role})`;
        badgeCH1.className = "badge bg-primary"; // Azul como el LED físico
        overlayTextCH1.innerText = `SIN SEÑAL SPI ${role}`;

        if (chartCH1) {
            reconfigurarDatasets(chartCH1, [
                { label: 'SCK', color: '#808080' },    // SCK (Gris)
                { label: 'MOSI', color: '#007bff' },   // MOSI (Azul)
                { label: 'MISO', color: '#999999' },   // MISO (Gris Visible)
                { label: 'CS', color: '#fd7e14' }      // CS (Naranja)
            ]);
        }
    }

    // Si es esclavo, enviar automáticamente la memoria al hardware al cambiar
    if (role === 'SLAVE') {
        setTimeout(() => { actualizarModoActivoHardware(); }, 500);
    }

    // FUERZA LA DESINCRONIZACIÓN: Cualquier cambio de modo requiere aplicar al hardware
    marcarProxyDesincronizado();
}

// ========== LÓGICA DE MODOS MAESTRO/ESCLAVO (Mejora 3) ==========

window.toggleRoleUI = function () {
    const role = document.getElementById('deviceRoleSelect').value;
    document.getElementById('slave-memory-config').style.display = (role === 'SLAVE') ? 'block' : 'none';
    document.getElementById('master-send-config').style.display = (role === 'MASTER') ? 'block' : 'none';

    if (role === 'SLAVE') {
        // Enviar configuración de esclavo al hardware inmediatamente
        actualizarModoActivoHardware();
    }
}

window.enviarMensajeActivo = function () {
    const rawVal = document.getElementById('protocoloSelect').value;
    const proto = rawVal.split('_')[0];
    const data = document.getElementById('masterDataInput').value;

    if (!wifiAnalizador || !wifiAnalizador.isConnected) return;

    if (proto === 'I2C') {
        const addr = prompt("Dirección I2C del Esclavo (ej: 0x3C)", "0x08");
        if (addr) wifiAnalizador.sendData(`I2C_SEND:${addr}:${data}`);
    } else if (proto === 'SPI') {
        wifiAnalizador.sendData(`SPI_SEND:${data}`);
    }
}

function actualizarModoActivoHardware() {
    const rawVal = document.getElementById('protocoloSelect').value;
    const proto = rawVal.split('_')[0];
    const role = rawVal.split('_')[1] || 'SNIFFER';

    if (role === 'SLAVE') {
        const memory = document.getElementById('slaveDataInput').value;
        wifiAnalizador.sendData(`${proto}_SLAVE_ON:${memory}`);
    } else if (role === 'SNIFFER') {
        enviarConfiguracionFisica();
    }
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
    const rawVal = document.getElementById('protocoloSelect').value;
    const proto = rawVal.split('_')[0];
    const role = rawVal.split('_')[1] || 'SNIFFER';

    let cmd = "CONFIG:" + proto + ":" + role + ":";

    if (proto === 'UART') {
        cmd += document.getElementById('baudSelect')?.value || "115200";
    } else if (proto === 'I2C') {
        const speed = document.getElementById('i2cSpeed')?.value || "100000";
        const addr = document.getElementById('i2cAddr')?.value || "0x00";
        const reg = document.getElementById('i2cReg')?.value || "0x00";
        cmd += `${speed}:${addr}:${reg}`;
    } else if (proto === 'SPI') {
        const freq = document.getElementById('spiFreq')?.value || "1000000";
        const mode = document.getElementById('spiMode')?.value || "0";
        cmd += `${freq}:${mode}`;
    }

    const applyBtn = document.getElementById('btn-aplicar-config-proxy');
    if (applyBtn) {
        applyBtn.disabled = true;
        applyBtn.innerHTML = '<span class="spinner-border spinner-border-sm"></span> Sincronizando...';
    }
    wifiAnalizador.sendData(cmd);

    if (role !== 'SNIFFER') {
        setTimeout(() => { actualizarModoActivoHardware(); }, 1500);
    }
}

export function marcarProxyDesincronizado() {
    const badge = document.getElementById('sync-status-proxy');
    const applyBtn = document.getElementById('btn-aplicar-config-proxy');
    if (badge) {
        badge.classList.remove('bg-success', 'bg-primary', 'text-white');
        badge.classList.add('bg-warning', 'text-dark');
        badge.innerHTML = '<i class="bi bi-exclamation-triangle"></i> Desincronizado';
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
        badge.className = "badge bg-success text-white px-3 py-2 shadow-sm animate__animated animate__bounceIn";
        badge.innerHTML = '<i class="bi bi-check-circle-fill"></i> Sincronizado';
    }
    if (applyBtn) {
        applyBtn.disabled = false;
        applyBtn.innerHTML = '<i class="bi bi-cpu"></i> Aplicar al Hardware';
    }
}

window.resetCharts = resetCharts;
window.clearData = clearData;

/**
 * Activa o desactiva el panel de configuración de protocolo
 */
function toggleProtocolConfig(enabled) {
    const panel = document.getElementById('panel-config-proxy');
    const select = document.getElementById('protocoloSelect');
    const applyBtn = document.getElementById('btn-aplicar-config-proxy');
    const configDinamica = document.getElementById('configuracionDinamica');

    if (select) select.disabled = !enabled;
    if (applyBtn) applyBtn.disabled = !enabled;

    if (configDinamica) {
        const inputs = configDinamica.querySelectorAll('input, select, button');
        inputs.forEach(el => el.disabled = !enabled);
    }

    if (panel) {
        panel.style.opacity = enabled ? "1" : "0.5";
        panel.style.pointerEvents = enabled ? "auto" : "none";
    }
}

/**
 * Reconfigura los datasets de un chart (colores y etiquetas)
 */
function reconfigurarDatasets(chart, config) {
    if (!chart) return;

    // Actualizar cada dataset según la configuración recibida
    config.forEach((c, i) => {
        if (chart.data.datasets[i]) {
            chart.data.datasets[i].label = c.label;
            chart.data.datasets[i].borderColor = c.color;
            // Asegurarnos de que el dataset sea visible
            chart.data.datasets[i].hidden = false;
        }
    });

    // Ocultar los datasets sobrantes (si los hay)
    for (let i = config.length; i < chart.data.datasets.length; i++) {
        chart.data.datasets[i].hidden = true;
        chart.data.datasets[i].label = ""; // Limpiar etiqueta para que el filtro de leyenda lo oculte
    }

    chart.update('none');
}

// Inicialización automática única y segura
document.addEventListener('DOMContentLoaded', () => {
    // Si el elemento chartCH1 existe en el DOM, inicializamos
    if (document.getElementById('chartCH1')) {
        initChart();
        startLiveClock();
        toggleProtocolConfig(false); // Iniciar deshabilitado hasta conectar

        // Pequeño retardo para asegurar que todo esté listo antes de la config inicial
        setTimeout(() => {
            actualizarConfiguracion('protocoloSelect');
        }, 300);
    }
});

// Exportación de funciones al objeto window para acceso desde el HTML
window.enviarConfiguracionFisica = enviarConfiguracionFisica;
window.actualizarConfiguracion = actualizarConfiguracion;
window.marcarProxyDesincronizado = marcarProxyDesincronizado;
window.marcarProxySincronizado = marcarProxySincronizado;
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
        toggleProtocolConfig(true);
        alert("✅ Conectado con éxito al Visualizador.");
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
        toggleProtocolConfig(false);
    };

    wifiAnalizador.onDataReceived = procesarEntradaAnalizador;
    const ipInput = document.getElementById('proxy-ip-addr');
    wifiAnalizador.connect(ipInput ? ipInput.value : "192.168.4.1");
};
