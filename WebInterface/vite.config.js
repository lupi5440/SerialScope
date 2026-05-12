import { resolve } from 'path'
import { defineConfig } from 'vite'
import { VitePWA } from 'vite-plugin-pwa'

export default defineConfig({
  root: '.',
  base: '/SerialScope/',
  plugins: [
    VitePWA({
      registerType: 'autoUpdate',
      includeAssets: ['favicon.ico', 'apple-touch-icon.png', 'mask-icon.svg'],
      manifest: {
        name: 'SerialScope',
        short_name: 'SerialScope',
        description: 'Potente herramienta de visualización y aprendizaje para protocolos seriales UART, I2C, SPI y RS232.',
        theme_color: '#667eea',
        background_color: '#ffffff',
        display: 'standalone',
        icons: [
          {
            src: 'pwa-192x192.png',
            sizes: '192x192',
            type: 'image/png'
          },
          {
            src: 'pwa-512x512.png',
            sizes: '512x512',
            type: 'image/png'
          },
          {
            src: 'pwa-512x512.png',
            sizes: '512x512',
            type: 'image/png',
            purpose: 'any maskable'
          }
        ]
      }
    })
  ],
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
