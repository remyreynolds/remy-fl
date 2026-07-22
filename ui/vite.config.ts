import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'
import path from 'node:path'

export default defineConfig({
  plugins: [react(), tailwindcss()],
  resolve: {
    alias: {
      '@': path.resolve(__dirname, './src'),
    },
  },
  build: {
    outDir: '../fl-midi-agent/relay/static',
    emptyOutDir: true,
  },
  server: {
    host: '127.0.0.1',
    proxy: {
      '/health': 'http://127.0.0.1:8765',
      '/feedback': 'http://127.0.0.1:8765',
      '/companion': 'http://127.0.0.1:8765',
    },
  },
})
