import './styles.css';
import { Header } from './components/Header.js';
import { Footer } from './components/Footer.js';

/**
 * Inyecta el Header y Footer en los placeholders correspondientes
 */
function initLayout() {
    const headerPlaceholder = document.getElementById('header-placeholder');
    const footerPlaceholder = document.getElementById('footer-placeholder');

    if (headerPlaceholder) {
        // Determinar página actual para resaltar el link activo
        const path = window.location.pathname;
        let currentPage = 'home';
        if (path.includes('visualizacion')) currentPage = 'visualizacion';
        else if (path.includes('pruebas')) currentPage = 'pruebas';
        else if (path.includes('referencias')) currentPage = 'referencias';
        else if (path.includes('educacion')) currentPage = 'educacion';

        headerPlaceholder.innerHTML = Header(currentPage);
    }

    if (footerPlaceholder) {
        footerPlaceholder.innerHTML = Footer();
    }
}

// Inicializar cuando el DOM esté listo
document.addEventListener('DOMContentLoaded', initLayout);

// Exportar utilidades comunes si es necesario
export { initLayout };
