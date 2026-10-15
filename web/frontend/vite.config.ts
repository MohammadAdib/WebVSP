import react from '@vitejs/plugin-react'
import fs from 'node:fs'
import path from 'node:path'
import { defineConfig, type Plugin } from 'vite'

// The page again at local/ (…/WebVSP/local/): the same app using the engine on this computer
// instead of the in-browser one (see src/api/wasmEngine.ts). A copy of index.html one folder
// down, with its relative asset paths adjusted.
function localPage(): Plugin {
  let outDir = 'dist'
  return {
    name: 'webvsp-local-page',
    apply: 'build',
    configResolved(config) {
      outDir = path.resolve(config.root, config.build.outDir)
    },
    closeBundle() {
      const html = fs.readFileSync(path.join(outDir, 'index.html'), 'utf8')
      fs.mkdirSync(path.join(outDir, 'local'), { recursive: true })
      fs.writeFileSync(path.join(outDir, 'local', 'index.html'), html.replace(/(src|href)="\.\//g, '$1="../'))
    },
  }
}

// Dev: start the backend as usual (web\vsp-web.cmd or `vsp-web --no-browser`), then `npm run dev`.
// The backend only accepts its own origin, so the proxy presents requests as coming from it.
const BACKEND = 'http://localhost:8765'

// Cross-origin isolation, for the in-browser engine's threads (?engine=wasm). Hosted copies
// get it from public/coi-sw.js instead.
const ISOLATION = {
  'Cross-Origin-Opener-Policy': 'same-origin',
  'Cross-Origin-Embedder-Policy': 'require-corp',
}

export default defineConfig({
  // Relative asset paths: the same build works when served by the engine at / and when
  // hosted under a sub-path (e.g. https://<user>.github.io/WebVSP/).
  base: './',
  plugins: [react(), localPage()],
  preview: { headers: ISOLATION },
  server: {
    headers: ISOLATION,
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
