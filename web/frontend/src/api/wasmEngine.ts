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
import { useViewer } from '../viewer/store'

interface VspModule {
  call(id: number, op: string, a: string, b: string, value: number): void
  mirrorEvent(json: string): void
  take(): string
  takeBinary(id: number): Uint8Array | null
  requestFull(): void
  processOutput(id: number, text: string): void
  processExit(id: number, code: number): void
  FS: {
    writeFile(path: string, data: Uint8Array): void
    readFile(path: string): Uint8Array
    stat(path: string): { mtime: Date; size: number; mode: number }
    mkdirTree(path: string): void
    analyzePath(path: string): { exists: boolean }
    readdir(path: string): string[]
    isFile(mode: number): boolean
  }
}

/** The page's address under /local/ (the local-engine copy of the page, see vite.config.ts). */
const LOCAL_PATH = /\/local\/?$/

/**
 * Which engine this page uses, from its address alone:
 *   - hosted (e.g. GitHub Pages), or the Vite dev server: the in-browser engine; under /local/
 *     (or with ?engine=local) the engine on this computer instead;
 *   - served by an engine (localhost): that engine, unless ?engine=wasm;
 *   - ?engine=http://localhost:PORT: that engine (File > New Window).
 */
function chooseWasm(): boolean {
  try {
    localStorage.removeItem('webvsp-engine-mode') // an earlier version remembered the choice
  } catch {
    /* storage unavailable */
  }
  const q = new URLSearchParams(window.location.search).get('engine')
  if (q === 'wasm') return true
  if (q) return false // ?engine=local, or a specific engine address
  if (LOCAL_PATH.test(window.location.pathname)) return false
  // The Vite dev server isn't an engine: like the hosted page (its /local/ proxies to one).
  if (import.meta.env.DEV) return true
  return !['localhost', '127.0.0.1', '[::1]'].includes(window.location.hostname)
}

export const isWasmEngine = chooseWasm()

/** This page with the other engine (the switch in the status bar): / <-> /local/. */
export function otherEngineUrl(): string {
  const url = new URL(window.location.href)
  url.searchParams.delete('engine')
  url.hash = ''
  const base = url.pathname.replace(LOCAL_PATH, '/').replace(/[^/]*$/, '')
  url.pathname = isWasmEngine ? base + 'local/' : base
  return url.toString()
}

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
  const { out, mirror, proc } = JSON.parse(vsp.take()) as {
    out: Record<string, unknown>[]
    mirror: Record<string, unknown>[]
    proc: ProcRequest[]
  }
  for (const r of proc) {
    if (r.t === 'spawn') runProgram(vsp, r)
    else if (r.t === 'kill') killProgram(vsp, r.id)
  }
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
      // Several files can be picked: all are copied in (e.g. a model with its VSPAERO results,
      // for Load Previous Results), and OpenVSP gets the one its filter asks for.
      const picked = await pickFiles(exts, types)
      if (!picked.length) return answer('')
      for (const f of picked) vsp.FS.writeFile(`${HOME}/${f.name}`, new Uint8Array(await f.arrayBuffer()))
      const main = picked.find((f) => exts.some((e) => f.name.toLowerCase().endsWith('.' + e.toLowerCase()))) ?? picked[0]
      return answer(`${HOME}/${main.name}`)
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

function pickFiles(exts: string[], types: unknown): Promise<File[]> {
  const w = window as unknown as { showOpenFilePicker?: (o: unknown) => Promise<FileSystemFileHandle[]> }
  if (w.showOpenFilePicker) {
    return w
      .showOpenFilePicker({ id: 'webvsp', types, excludeAcceptAllOption: false, multiple: true })
      .then((handles) => Promise.all(handles.map((h) => h.getFile())))
      .catch(() => [])
  }
  return new Promise((resolve) => {
    const input = document.createElement('input')
    input.type = 'file'
    input.multiple = true
    if (exts.length) input.accept = exts.map((e) => '.' + e).join(',')
    input.onchange = () => resolve([...(input.files ?? [])])
    input.oncancel = () => resolve([])
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

// ------------------------------------------------------------------ programs

/**
 * OpenVSP "starting" one of VSPAERO's programs (src/util/WebProcess.h): each run is a worker
 * (public/program.worker.js) with a fresh instance of the program, compiled to WebAssembly.
 * It gets copies of the files in the working folders, its output is fed back as it runs, and
 * the files it wrote are copied back before OpenVSP is told it has exited.
 */
interface ProcRequest {
  t: 'spawn' | 'kill'
  id: number
  cmd?: string
  args?: string[]
  cwd?: string
}

const programs = new Map<number, Worker>()

function dirname(path: string) {
  const i = path.lastIndexOf('/')
  return i <= 0 ? '/' : path.slice(0, i)
}

function runProgram(vsp: VspModule, r: ProcRequest) {
  const cmd = (r.cmd ?? '').split('/').pop()!.replace(/\.exe$/i, '')
  const args = r.args ?? []
  if (cmd === 'vspviewer') return openViewer(vsp, r.id, args)
  const cwd = r.cwd || '/'
  // The folders the program reads and writes: the user's files, the working folder, and the
  // folders of any paths among its arguments (VSPAERO gets the model's path).
  const dirs = [...new Set([HOME, cwd, ...args.filter((a) => a.startsWith('/')).map(dirname)])]
  const files: { path: string; data: Uint8Array }[] = []
  for (const dir of dirs) {
    if (!vsp.FS.analyzePath(dir).exists) continue
    for (const name of vsp.FS.readdir(dir)) {
      const path = (dir === '/' ? '' : dir) + '/' + name
      if (name === '.' || name === '..' || !vsp.FS.isFile(vsp.FS.stat(path).mode)) continue
      files.push({ path, data: new Uint8Array(vsp.FS.readFile(path)) }) // ordinary (not shared) copies
    }
  }

  const worker = new Worker(new URL(`${import.meta.env.BASE_URL}program.worker.js`, window.location.href), { type: 'module' })
  programs.set(r.id, worker)
  const finish = (code: number) => {
    if (!programs.has(r.id)) return
    programs.delete(r.id)
    worker.terminate()
    vsp.processExit(r.id, code)
  }
  worker.onmessage = ({ data }) => {
    if (data.type === 'out') vsp.processOutput(r.id, data.text)
    else if (data.type === 'exit') {
      for (const f of data.files as { path: string; data: Uint8Array }[]) {
        vsp.FS.mkdirTree(dirname(f.path))
        vsp.FS.writeFile(f.path, f.data)
      }
      finish(data.code)
    }
  }
  worker.onerror = (e) => {
    vsp.processOutput(r.id, `${cmd}: ${e.message || 'failed to start'}\n`)
    finish(127)
  }
  worker.postMessage({ cmd, args, cwd, dirs, files }, files.map((f) => f.data.buffer))
}

function killProgram(vsp: VspModule, id: number) {
  const worker = programs.get(id)
  if (!worker) return
  programs.delete(id)
  worker.terminate()
  vsp.processOutput(id, 'Stopped.\n')
  vsp.processExit(id, 143)
}

/**
 * VSPAERO's viewer: shown in the page (src/viewer) instead of a desktop program. OpenVSP sees
 * it running until it's closed. Its last argument is the model's base path (<base>.adb).
 */
function openViewer(vsp: VspModule, id: number, args: string[]) {
  const base = (args[args.length - 1] ?? '').replace(/\.adb$/, '')
  const read = (path: string) => (vsp.FS.analyzePath(path).exists ? new Uint8Array(vsp.FS.readFile(path)) : null)
  const adb = read(`${base}.adb`)
  const cases = read(`${base}.adb.cases`)
  const polar = read(`${base}.polar`)
  if (!adb) {
    vsp.processOutput(id, `vspviewer: ${base}.adb not found\n`)
    vsp.processExit(id, 1)
    return
  }
  const name = base.split('/').pop() || base
  const text = (b: Uint8Array | null) => (b ? new TextDecoder().decode(b) : '')
  useViewer.getState().show(name, adb.buffer, text(cases), text(polar), () => vsp.processExit(id, 0))
}
