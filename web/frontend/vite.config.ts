import react from '@vitejs/plugin-react'
import { defineConfig } from 'vite'

// Cross-origin isolation, for OpenVSP's threads (SharedArrayBuffer). Hosted copies get it
// from public/coi-sw.js instead, since static hosts can't send these headers.
const ISOLATION = {
  'Cross-Origin-Opener-Policy': 'same-origin',
  'Cross-Origin-Embedder-Policy': 'require-corp',
}

export default defineConfig({
  // Relative asset paths: the same build works at / and under a sub-path
  // (e.g. https://<user>.github.io/WebVSP/).
  base: './',
  plugins: [react()],
  preview: { headers: ISOLATION },
  server: { headers: ISOLATION },
})
