import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import './index.css'
import App from './App.tsx'
import { isWasmEngine } from './api/wasmEngine'

/**
 * The in-browser engine runs OpenVSP on its own thread, which needs a cross-origin isolated
 * page. Where the server doesn't send the headers (e.g. GitHub Pages), a service worker adds
 * them (public/coi-sw.js); the page reloads once it is in control.
 */
async function isolate(): Promise<boolean> {
  if (!isWasmEngine || window.crossOriginIsolated) return true
  if (!('serviceWorker' in navigator)) return false
  const KEY = 'webvsp-coi-reload'
  try {
    await navigator.serviceWorker.register(`${import.meta.env.BASE_URL}coi-sw.js`)
    await navigator.serviceWorker.ready
    if (!sessionStorage.getItem(KEY)) {
      sessionStorage.setItem(KEY, '1')
      window.location.reload()
      return new Promise(() => {}) // reloading
    }
  } catch (err) {
    console.error('WebVSP: could not set up the in-browser engine', err)
  }
  sessionStorage.removeItem(KEY)
  return false
}

void isolate().then((ok) => {
  if (!ok) console.error('WebVSP: this page is not cross-origin isolated; the in-browser engine cannot start')
  createRoot(document.getElementById('root')!).render(
    <StrictMode>
      <App />
    </StrictMode>,
  )
})
