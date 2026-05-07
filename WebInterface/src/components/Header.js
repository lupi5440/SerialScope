// src/components/Header.js

export function Header(currentPage) {
    return `
<nav class="navbar navbar-expand-lg navbar-dark shadow-lg sticky-top">
    <div class="container">
        <a class="navbar-brand d-flex align-items-center" href="/index.html">
            <img src="/img/icono.png" alt="SerialScope"
                style="height: 48px; margin-right: 16px; filter: drop-shadow(0 2px 6px rgba(0, 0, 0, 0.8)) brightness(1.3) contrast(1.3) saturate(1.4);">
            <span
                style="background: linear-gradient(180deg, #0a1628 0%, #1e3a5f 100%); -webkit-background-clip: text; -webkit-text-fill-color: transparent; background-clip: text; font-size: 1.5rem; font-weight: 900; letter-spacing: 0.5px; filter: drop-shadow(0 2px 4px rgba(255, 255, 255, 0.8));">SerialScope</span>
        </a>
        <button class="navbar-toggler" type="button" data-bs-toggle="collapse" data-bs-target="#navbarNav">
            <span class="navbar-toggler-icon"></span>
        </button>
        <div class="collapse navbar-collapse" id="navbarNav">
            <ul class="navbar-nav ms-auto">
                <li class="nav-item">
                    <a class="nav-link ${currentPage === 'home' ? 'active' : ''}" href="/index.html">Inicio</a>
                </li>
                <li class="nav-item dropdown">
                    <a class="nav-link dropdown-toggle" href="#" id="navbarDropdown" role="button" data-bs-toggle="dropdown">
                        Sección informativa
                    </a>
                    <ul class="dropdown-menu">
                        <li><a class="dropdown-item" href="/pages/educacion.html">Fundamentos de comunicación</a></li>
                        <li><hr class="dropdown-divider"></li>
                        <li><a class="dropdown-item" href="/pages/uart.html">UART</a></li>
                        <li><a class="dropdown-item" href="/pages/rs232.html">RS232</a></li>
                        <li><a class="dropdown-item" href="/pages/i2c.html">I2C</a></li>
                        <li><a class="dropdown-item" href="/pages/spi.html">SPI</a></li>
                    </ul>
                </li>
                <li class="nav-item">
                    <a class="nav-link ${currentPage === 'visualizacion' ? 'active' : ''}" href="/pages/visualizacion.html">Visualizador</a>
                </li>
                <li class="nav-item">
                    <a class="nav-link ${currentPage === 'pruebas' ? 'active' : ''}" href="/pages/pruebas.html">Probador</a>
                </li>
                <li class="nav-item">
                    <a class="nav-link ${currentPage === 'referencias' ? 'active' : ''}" href="/pages/referencias.html">Referencias</a>
                </li>
            </ul>
        </div>
    </div>
</nav>
    `;
}
