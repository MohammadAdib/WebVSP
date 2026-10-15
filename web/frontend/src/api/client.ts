import type { BBox, DirListing, GeomSchema, ModelTree, ParmValues, ServerEvent } from './types'
import { isWasmEngine, wasmConnect, wasmRequest, WasmEngineError } from './wasmEngine'

// The backend passes its session token in the URL fragment (#token=...) so it never
// hits server logs; a desktop shell can inject `window.__VSPWEB__` instead.
declare global {
  interface Window {
    __VSPWEB__?: { baseUrl?: string; token?: string }
  }
}

function readConfig() {
  const injected = window.__VSPWEB__ ?? {}
  let token = injected.token ?? null
  const m = /token=([^&]+)/.exec(window.location.hash)
  if (m) {
    token = decodeURIComponent(m[1])
    try {
      sessionStorage.setItem('vspweb-token', token)
    } catch {
      /* storage unavailable */
    }
    history.replaceState(null, '', window.location.pathname + window.location.search)
  }
  if (!token) {
    try {
      token = sessionStorage.getItem('vspweb-token')
    } catch {
      /* storage unavailable */
    }
  }
  return { baseUrl: injected.baseUrl ?? engineUrl(), token }
}

/**
 * Where the WebVSP engine (the local backend) is. When the engine serves this page
 * (or the Vite dev proxy does), it's the same origin. A hosted copy of the UI (e.g. GitHub
 * Pages) talks to the engine running on the user's own machine.
 */
function engineUrl(): string {
  const local = ['localhost', '127.0.0.1', '[::1]'].includes(window.location.hostname)
  if (local) return ''
  // Per tab, not remembered: each window (File > New Window) has an engine of its own.
  try {
    localStorage.removeItem('webvsp-engine') // older versions remembered it for every tab
  } catch {
    /* storage unavailable */
  }
  const fromQuery = new URLSearchParams(window.location.search).get('engine')
  return fromQuery ? fromQuery.replace(/\/$/, '') : DEFAULT_ENGINE
}

export const DEFAULT_ENGINE = 'http://localhost:8765'

const config = readConfig()

/** True when this page is a hosted copy of the UI that talks to a local engine. */
/** Hosted copy of the UI talking to an engine on this machine (not the in-browser engine). */
export const isHostedUI = config.baseUrl !== '' && !isWasmEngine
export const engineAddress = config.baseUrl || window.location.origin

export class ApiError extends Error {
  status: number
  constructor(status: number, message: string) {
    super(message)
    this.status = status
  }
}

async function request<T>(method: string, path: string, body?: unknown): Promise<T> {
  if (isWasmEngine) {
    try {
      return (await wasmRequest(method, path, body)) as T
    } catch (err) {
      if (err instanceof WasmEngineError) throw new ApiError(err.status, err.message)
      throw err
    }
  }
  const headers: Record<string, string> = {}
  if (config.token) headers['x-vsp-token'] = config.token
  if (body !== undefined) headers['content-type'] = 'application/json'
  const res = await fetch(config.baseUrl + '/api' + path, {
    method,
    headers,
    body: body === undefined ? undefined : JSON.stringify(body),
  })
  if (!res.ok) {
    let msg = res.statusText
    try {
      const j = await res.json()
      msg = typeof j.detail === 'string' ? j.detail : JSON.stringify(j.detail)
    } catch {
      /* not json */
    }
    throw new ApiError(res.status, msg)
  }
  const type = res.headers.get('content-type') ?? ''
  if (type.includes('application/json')) return res.json() as Promise<T>
  return res.arrayBuffer() as Promise<T>
}

const enc = encodeURIComponent

export const api = {
  model: () => request<ModelTree>('GET', '/model'),
  geomTypes: () => request<string[]>('GET', '/geom-types'),
  bbox: () => request<BBox | null>('GET', '/bbox'),

  addGeom: (type: string, parent?: string | null) =>
    request<{ id: string }>('POST', '/geoms', { type, parent: parent ?? null }),
  deleteGeoms: (ids: string[]) => request('POST', '/geoms/delete', { ids }),
  cutGeom: (id: string) => request('POST', `/geoms/${enc(id)}/cut`),
  copyGeom: (id: string) => request('POST', `/geoms/${enc(id)}/copy`),
  pasteGeom: (parent?: string | null) => request<{ ids: string[] }>('POST', '/geoms/paste', { parent: parent ?? null }),
  renameGeom: (id: string, name: string) => request('PUT', `/geoms/${enc(id)}/name`, { name }),
  setShown: (id: string, shown: boolean) => request('PUT', `/geoms/${enc(id)}/shown`, { shown }),

  geomParms: (id: string) => request<GeomSchema>('GET', `/geoms/${enc(id)}/parms`),
  geomValues: (id: string) => request<ParmValues>('GET', `/geoms/${enc(id)}/values`),
  mesh: (id: string) => request<ArrayBuffer>('GET', `/geoms/${enc(id)}/mesh`),

  insertXSec: (geomId: string, index: number, shape: number) =>
    request('POST', `/geoms/${enc(geomId)}/xsecs`, { index, shape }),
  cutXSec: (geomId: string, index: number) => request('DELETE', `/geoms/${enc(geomId)}/xsecs/${index}`),
  changeXSecShape: (xsecSurfId: string, index: number, shape: number) =>
    request('PUT', '/xsec-shape', { xsecSurfId, index, shape }),

  setParm: (id: string, value: number) => request('PUT', `/parms/${enc(id)}`, { value }),

  undo: () => request<{ label: string | null }>('POST', '/undo'),
  redo: () => request<{ label: string | null }>('POST', '/redo'),

  newFile: () => request('POST', '/file/new'),
  openFile: (path: string) => request('POST', '/file/open', { path }),
  insertFile: (path: string, parent?: string | null) => request('POST', '/file/insert', { path, parent: parent ?? null }),
  saveFile: (path?: string) => request<{ path: string }>('POST', '/file/save', { path: path ?? null }),
  pick: (id: string) => request('POST', '/pick', { id }),
  newInstance: () => request<{ port: number }>('POST', '/instances'),
  listDir: (path?: string) => request<DirListing>('GET', '/fs' + (path ? `?path=${enc(path)}` : '')),
}

/** Socket messages that aren't model events: GUI mirror snapshots/diffs and prompt bookkeeping. */
// eslint-disable-next-line @typescript-eslint/no-explicit-any
export type SocketMessage = ServerEvent | { type: 'mirror_snapshot' | 'mirror' | 'dialog_done'; [k: string]: any }

export function connectEvents(onEvent: (e: SocketMessage) => void, onStatus: (connected: boolean) => void) {
  if (isWasmEngine) return wasmConnect(onEvent, onStatus)
  let ws: WebSocket | null = null
  let closed = false
  let retry = 500

  const open = () => {
    const base = config.baseUrl || window.location.origin
    const url = new URL('/api/ws', base)
    url.protocol = url.protocol === 'https:' ? 'wss:' : 'ws:'
    if (config.token) url.searchParams.set('token', config.token)
    ws = new WebSocket(url)
    ws.onopen = () => {
      retry = 500
      onStatus(true)
    }
    ws.onmessage = (m) => onEvent(JSON.parse(m.data))
    ws.onclose = () => {
      onStatus(false)
      if (!closed) setTimeout(open, (retry = Math.min(retry * 2, 5000)))
    }
  }
  open()
  return {
    close: () => {
      closed = true
      ws?.close()
    },
    send: (msg: unknown) => {
      if (ws?.readyState === WebSocket.OPEN) ws.send(JSON.stringify(msg))
    },
  }
}
