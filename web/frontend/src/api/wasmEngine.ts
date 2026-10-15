/**
 * The in-browser engine: OpenVSP compiled to WebAssembly (web/wasm/engine/engine.cpp), running
 * in this tab instead of a local engine process. Open the UI with ?engine=wasm.
 *
 * It stands in for the local engine's HTTP API and WebSocket:
 *   - requests (the same paths as web/backend/vspweb/app.py) are queued to OpenVSP's thread;
 *   - a fast loop collects their results, model-change events and the mirrored GUI's output,
 *     and hands the events to the UI exactly as the WebSocket would;
 *   - GUI input and dialog answers go straight to the mirror.
 * File choosers in the GUI use the browser's file pickers; files live in the module's
 * in-memory file system (see answerFileDialog below).
 *
 * OpenVSP runs on its own thread, which needs a cross-origin isolated page (SharedArrayBuffer);
 * main.tsx arranges that before the app starts.
 */
import type { SocketMessage } from './client'
import type { MDialog } from '../mirror/types'

interface VspModule {
  call(id: number, op: string, a: string, b: string, value: number): void
  mirrorEvent(json: string): void
  take(): string
  takeBinary(id: number): Uint8Array | null
  requestFull(): void
  FS: {
    writeFile(path: string, data: Uint8Array): void
    readFile(path: string): Uint8Array
    stat(path: string): { mtime: Date; size: number }
    mkdirTree(path: string): void
    analyzePath(path: string): { exists: boolean }
  }
}

export const isWasmEngine = new URLSearchParams(window.location.search).get('engine') === 'wasm'

export class WasmEngineError extends Error {
  status: number
  constructor(status: number, message: string) {
    super(message)
    this.status = status
  }
}

type Pending = { resolve: (v: unknown) => void; reject: (e: unknown) => void }

let modulePromise: Promise<VspModule> | null = null
let nextId = 1
const pending = new Map<number, Pending>()
const listeners = new Set<(e: SocketMessage) => void>()
let ready: (() => void) | null = null
const readyPromise = new Promise<void>((r) => (ready = r))

function emit(e: SocketMessage) {
  listeners.forEach((fn) => fn(e))
}

function load(): Promise<VspModule> {
  modulePromise ??= (async () => {
    // Served as static files (web/frontend/public/wasm), not bundled: large and optional.
    const url = new URL(`${import.meta.env.BASE_URL}wasm/vsp.js`, window.location.href).href
    const { default: createVspModule } = await import(/* @vite-ignore */ url)
    const vsp = (await createVspModule()) as VspModule
    const tick = () => {
      try {
        drain(vsp)
      } catch (err) {
        console.error('WebVSP engine:', err)
      }
    }
    // Faster than the GUI's 30 Hz refresh; keeps going in background tabs (unlike rAF).
    setInterval(tick, 16)
    return vsp
  })()
  return modulePromise
}

function drain(vsp: VspModule) {
  const { out, mirror } = JSON.parse(vsp.take()) as { out: Record<string, unknown>[]; mirror: Record<string, unknown>[] }
  for (const m of out) {
    if (m.t === 'result') {
      const p = pending.get(m.id as number)
      if (!p) continue
      pending.delete(m.id as number)
      if (!m.ok) p.reject(new WasmEngineError(400, String(m.error)))
      else if (m.bin) {
        const bytes = vsp.takeBinary(m.id as number)
        p.resolve(bytes ? bytes.buffer : new ArrayBuffer(0))
      } else p.resolve(m.value)
    } else if (m.type === 'engine_ready') {
      if (m.gui) {
        emit({ type: 'mirror_snapshot', enabled: true, windows: [], widgets: [], dialogs: [] })
        vsp.requestFull()
      }
      ready?.()
    } else {
      emit(m as unknown as SocketMessage)
    }
  }
  if (mirror.length) {
    const files = mirror.filter((m) => m.t === 'dialog' && m.kind === 'file')
    // Answered here with the browser's pickers, not the UI's file browser.
    for (const m of files) m.native = true
    // The UI must know a prompt before it hears it was answered.
    emit({ type: 'mirror', msgs: mirror })
    for (const m of files) void answerFileDialog(vsp, m as unknown as MDialog)
  }
  watchSaves(vsp)
}

async function call(op: string, a = '', b = '', value = 0): Promise<unknown> {
  const vsp = await load()
  await readyPromise
  const id = nextId++
  return new Promise((resolve, reject) => {
    pending.set(id, { resolve, reject })
    vsp.call(id, op, a, b, value)
  })
}

/** Same contract as the HTTP request(): JSON results as objects, meshes as ArrayBuffers. */
export async function wasmRequest(method: string, path: string, body?: unknown): Promise<unknown> {
  const b = (body ?? {}) as Record<string, unknown>
  const route = `${method} ${path.split('?')[0]}`
  let m: RegExpMatchArray | null

  if (route === 'GET /model') {
    const tree = (await call('model')) as { fileName: string }
    return { ...tree, fileName: displayName(tree.fileName) }
  }
  if (route === 'GET /geom-types') return call('geomTypes')
  if (route === 'GET /bbox') return call('bbox')
  if ((m = route.match(/^GET \/geoms\/([^/]+)\/mesh$/))) return call('mesh', decodeURIComponent(m[1]))
  if (route === 'POST /geoms') return call('addGeom', String(b.type), b.parent ? String(b.parent) : '')
  if (route === 'POST /geoms/delete') return call('deleteGeoms', (b.ids as string[]).join(','))
  if ((m = route.match(/^PUT \/parms\/([^/]+)$/))) return call('setParm', decodeURIComponent(m[1]), '', Number(b.value))
  if (route === 'POST /file/new') return call('newModel')
  if (route === 'POST /pick') return call('pick', String(b.id ?? ''))
  throw new WasmEngineError(501, `Not available in the in-browser engine: ${method} ${path}`)
}

// For the browser console and tests: __webvspEngine.request('POST', '/geoms', { type: 'WING' })
if (isWasmEngine) (window as unknown as Record<string, unknown>).__webvspEngine = { request: wasmRequest }

/** Stand-in for the WebSocket: "connected" once OpenVSP has started, then its events. */
export function wasmConnect(onEvent: (e: SocketMessage) => void, onStatus: (connected: boolean) => void) {
  listeners.add(onEvent)
  let closed = false
  load()
    .then(() => readyPromise)
    .then(
      () => !closed && onStatus(true),
      (err) => {
        console.error('WebVSP: the in-browser engine failed to load', err)
        onStatus(false)
      },
    )
  return {
    close: () => {
      closed = true
      listeners.delete(onEvent)
    },
    send: (msg: unknown) => {
      const m = msg as { type?: string; event?: Record<string, unknown> }
      if (m.type !== 'mirror_event' || !m.event) return
      // The mirror reads flat JSON: numbers and strings only (as the Python relay sends).
      const flat: Record<string, string | number> = {}
      for (const [k, v] of Object.entries(m.event)) flat[k] = typeof v === 'number' || typeof v === 'string' ? v : String(v)
      void load().then((vsp) => vsp.mirrorEvent(JSON.stringify(flat)))
    },
  }
}

// ------------------------------------------------------------------ files

/** Folder in the module's file system for files the user opens or saves. */
const HOME = '/home/web_user'

function displayName(path: string) {
  return path.startsWith(HOME + '/') ? path.slice(HOME.length + 1) : path.replace(/^\//, '')
}

/** Extensions in an FLTK filter like "*.vsp3", "*.{af,dat}" or "Images (*.{jpg,png})". */
function filterExts(filter?: string): string[] {
  const exts: string[] = []
  for (const m of (filter ?? '').matchAll(/\*\.(\{[^}]*\}|[\w]+)/g)) {
    exts.push(...m[1].replace(/[{}]/g, '').split(',').map((e) => e.trim()))
  }
  return exts.filter((e) => e && e !== '*')
}

interface SaveTarget {
  path: string
  handle: FileSystemFileHandle | null
  /** Modification time before the save; then the last one seen, until it stops changing. */
  mtime: number
  size: number
  changed: boolean
  name: string
}
const saves: SaveTarget[] = []

/**
 * A GUI file chooser: pick with the browser, then answer with a path in the module's file
 * system. Opened files are copied in first; saved files are copied out once OpenVSP has
 * written them (watchSaves). Chrome/Edge's file pickers remember the last folder.
 */
async function answerFileDialog(vsp: VspModule, d: MDialog) {
  const answer = (path: string) => {
    vsp.mirrorEvent(JSON.stringify({ op: 'dialog', seq: d.seq, result: path ? 1 : 0, path }))
    emit({ type: 'dialog_done', seq: d.seq })
  }
  const exts = filterExts(d.filter)
  const types = exts.length
    ? [{ description: exts.map((e) => e.toUpperCase()).join(', ') + ' files', accept: { 'application/octet-stream': exts.map((e) => '.' + e) } }]
    : undefined
  try {
    vsp.FS.mkdirTree(HOME)
    if (!d.mode) {
      const file = await pickFile(exts, types)
      if (!file) return answer('')
      const path = `${HOME}/${file.name}`
      vsp.FS.writeFile(path, new Uint8Array(await file.arrayBuffer()))
      return answer(path)
    }
    const suggested = (d.preset || '').split(/[\\/]/).pop() || (exts[0] ? `Unnamed.${exts[0]}` : 'Unnamed')
    const w = window as unknown as { showSaveFilePicker?: (o: unknown) => Promise<FileSystemFileHandle> }
    let handle: FileSystemFileHandle | null = null
    let name = suggested
    if (w.showSaveFilePicker) {
      try {
        handle = await w.showSaveFilePicker({ id: 'webvsp', suggestedName: suggested, types })
      } catch {
        return answer('') // cancelled
      }
      name = handle.name
    }
    const path = `${HOME}/${name}`
    const before = vsp.FS.analyzePath(path).exists ? vsp.FS.stat(path).mtime.getTime() : 0
    saves.push({ path, handle, mtime: before, size: -1, changed: false, name })
    answer(path)
  } catch (err) {
    console.error('WebVSP: file dialog failed', err)
    answer('')
  }
}

function pickFile(exts: string[], types: unknown): Promise<File | null> {
  const w = window as unknown as { showOpenFilePicker?: (o: unknown) => Promise<FileSystemFileHandle[]> }
  if (w.showOpenFilePicker) {
    return w
      .showOpenFilePicker({ id: 'webvsp', types, excludeAcceptAllOption: false })
      .then((h) => h[0].getFile())
      .catch(() => null)
  }
  return new Promise((resolve) => {
    const input = document.createElement('input')
    input.type = 'file'
    if (exts.length) input.accept = exts.map((e) => '.' + e).join(',')
    input.onchange = () => resolve(input.files?.[0] ?? null)
    input.oncancel = () => resolve(null)
    input.click()
  })
}

/** Copy out files OpenVSP has finished writing for a save dialog (written, then unchanged). */
function watchSaves(vsp: VspModule) {
  for (let i = saves.length - 1; i >= 0; i--) {
    const s = saves[i]
    if (!vsp.FS.analyzePath(s.path).exists) continue
    const st = vsp.FS.stat(s.path)
    const mtime = st.mtime.getTime()
    if (!s.changed) {
      if (mtime !== s.mtime) Object.assign(s, { changed: true, mtime, size: st.size })
      continue
    }
    if (mtime !== s.mtime || st.size !== s.size) {
      Object.assign(s, { mtime, size: st.size }) // still being written
      continue
    }
    saves.splice(i, 1)
    // A copy in an ordinary buffer: the module's memory is shared (threads).
    const bytes = new Uint8Array(vsp.FS.readFile(s.path))
    void deliver(s, bytes)
  }
}

async function deliver(s: SaveTarget, bytes: Uint8Array<ArrayBuffer>) {
  if (s.handle) {
    const out = await s.handle.createWritable()
    await out.write(bytes)
    await out.close()
    return
  }
  const a = document.createElement('a')
  a.href = URL.createObjectURL(new Blob([bytes]))
  a.download = s.name
  a.click()
  setTimeout(() => URL.revokeObjectURL(a.href), 10000)
}
