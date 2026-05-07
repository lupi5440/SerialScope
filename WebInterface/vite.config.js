import { resolve } from 'path'
import { defineConfig } from 'vite'

export default defineConfig({
  root: '.',
  base: './',
  build: {
    rollupOptions: {
      input: {
        main: resolve(__dirname, 'index.html'),
        educacion: resolve(__dirname, 'pages/educacion.html'),
        pruebas: resolve(__dirname, 'pages/pruebas.html'),
        i2c: resolve(__dirname, 'pages/i2c.html'),
        referencias: resolve(__dirname, 'pages/referencias.html'),
        rs232: resolve(__dirname, 'pages/rs232.html'),
        visualizacion: resolve(__dirname, 'pages/visualizacion.html'),
        spi: resolve(__dirname, 'pages/spi.html'),
        uart: resolve(__dirname, 'pages/uart.html'),
      },
    },
    outDir: 'dist', // Esta es la carpeta que se subirá al ESP32
    emptyOutDir: true,
  },
})
