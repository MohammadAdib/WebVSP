import react from '@vitejs/plugin-react'
import { defineConfig } from 'vite'

// Dev: start the backend as usual (web\vsp-web.cmd or `vsp-web --no-browser`), then `npm run dev`.
// The backend only accepts its own origin, so the proxy presents requests as coming from it.
const BACKEND = 'http://localhost:8765'

export default defineConfig({
  // Relative asset paths: the same build works when served by the engine at / and when
  // hosted under a sub-path (e.g. https://<user>.github.io/WebVSP/).
  base: './',
  plugins: [react()],
  server: {
    proxy: {
      '/api': {
        target: BACKEND,
        ws: true,
        changeOrigin: true,
        configure: (proxy) => {
          proxy.on('proxyReq', (req) => req.setHeader('origin', BACKEND))
          proxy.on('proxyReqWs', (req) => req.setHeader('origin', BACKEND))
        },
      },
    },
  },
})
