import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// https://vite.dev/config/
export default defineConfig({
  plugins: [react()],
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
