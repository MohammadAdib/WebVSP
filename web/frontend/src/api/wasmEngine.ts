/**
 * The engine: OpenVSP compiled to WebAssembly (web/wasm/engine/engine.cpp), running in this tab.
 *
 *   - requests (method + path, e.g. GET /geoms/{id}/mesh; see wasmRequest) are queued to
 *     OpenVSP's thread;
 *   - a fast loop collects their results, model-change events and the mirrored GUI's output,
 *     and hands the events to the page (wasmConnect);
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
  watchLinked(vsp)
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
  if (route === 'POST /pick') return call('pick', String(b.id ?? ''), b.toggle ? '1' : '')
  if ((m = route.match(/^GET \/geoms\/([^/]+)\/origin$/))) return call('origin', decodeURIComponent(m[1]))
  if (route === 'POST /geoms/move') return call('move', (b.ids as string[]).join(','), (b.delta as number[]).join(','))
  throw new WasmEngineError(501, `Not available in the in-browser engine: ${method} ${path}`)
}

// For the browser console and tests: __webvspEngine.request('POST', '/geoms', { type: 'WING' })
(window as unknown as Record<string, unknown>).__webvspEngine = { request: wasmRequest }

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
      // The mirror reads flat JSON: numbers and strings only.
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

/**
 * A file in the module's file system that stands for one on the user's disk: a model the user
 * opened, or saved through a save dialog. Whenever OpenVSP writes it (Save, Save As, or any
 * other write), the new contents go back to the user's file: through its handle (Chrome/Edge,
 * overwriting it in place) or as a download (other browsers).
 */
interface LinkedFile {
  path: string
  handle: FileSystemFileHandle | null
  name: string
  /** Modification time and size last seen; a change starts a write-back once they settle. */
  mtime: number
  size: number
  /** Written since the last write-back, waiting for the file to stop changing */
  pending: boolean
  /** Being written back */
  writing: boolean
}
const linked = new Map<string, LinkedFile>()

function link(vsp: VspModule, path: string, handle: FileSystemFileHandle | null, name: string) {
  const exists = vsp.FS.analyzePath(path).exists
  const st = exists ? vsp.FS.stat(path) : null
  linked.set(path, { path, handle, name, mtime: st ? st.mtime.getTime() : 0, size: st ? st.size : -1, pending: false, writing: false })
}

/**
 * A GUI file chooser: pick with the browser, then answer with a path in the module's file
 * system. Opened files are copied in first; files OpenVSP writes are copied back out
 * (watchLinked). Chrome/Edge's file pickers remember the last folder.
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
      const main = picked.find((f) => exts.some((e) => f.file.name.toLowerCase().endsWith('.' + e.toLowerCase()))) ?? picked[0]
      const path = `${HOME}/${main.name}`
      // File > Save writes the opened file again: that goes back to the file on disk.
      link(vsp, path, main.handle, main.name)
      if (main.handle) void askToWrite(main.handle)
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
    link(vsp, path, handle, name)
    answer(path)
  } catch (err) {
    console.error('WebVSP: file dialog failed', err)
    answer('')
  }
}

interface Picked {
  file: File
  name: string
  handle: FileSystemFileHandle | null
  arrayBuffer: () => Promise<ArrayBuffer>
}

function pickFiles(exts: string[], types: unknown): Promise<Picked[]> {
  const picked = (file: File, handle: FileSystemFileHandle | null): Picked => ({
    file,
    name: file.name,
    handle,
    arrayBuffer: () => file.arrayBuffer(),
  })
  const w = window as unknown as { showOpenFilePicker?: (o: unknown) => Promise<FileSystemFileHandle[]> }
  if (w.showOpenFilePicker) {
    return w
      .showOpenFilePicker({ id: 'webvsp', types, excludeAcceptAllOption: false, multiple: true })
      .then((handles) => Promise.all(handles.map(async (h) => picked(await h.getFile(), h))))
      .catch(() => [])
  }
  return new Promise((resolve) => {
    const input = document.createElement('input')
    input.type = 'file'
    input.multiple = true
    if (exts.length) input.accept = exts.map((e) => '.' + e).join(',')
    input.onchange = () => resolve([...(input.files ?? [])].map((f) => picked(f, null)))
    input.oncancel = () => resolve([])
    input.click()
  })
}

type PermissionHandle = FileSystemFileHandle & {
  queryPermission?: (o: { mode: 'readwrite' }) => Promise<PermissionState>
  requestPermission?: (o: { mode: 'readwrite' }) => Promise<PermissionState>
}

/** Ask now, while the user is choosing the file, to be allowed to save back to it later. */
async function askToWrite(handle: FileSystemFileHandle) {
  const h = handle as PermissionHandle
  try {
    if ((await h.queryPermission?.({ mode: 'readwrite' })) !== 'granted') await h.requestPermission?.({ mode: 'readwrite' })
  } catch {
    /* asked again when saving */
  }
}

/** Copy out linked files OpenVSP has finished writing (changed, then unchanged for a moment). */
function watchLinked(vsp: VspModule) {
  for (const f of linked.values()) {
    if (f.writing || !vsp.FS.analyzePath(f.path).exists) continue
    const st = vsp.FS.stat(f.path)
    const mtime = st.mtime.getTime()
    const changed = mtime !== f.mtime || st.size !== f.size
    if (changed) {
      Object.assign(f, { mtime, size: st.size, pending: true })
      continue
    }
    if (!f.pending) continue
    f.pending = false
    // A copy in an ordinary buffer: the module's memory is shared (threads).
    const bytes = new Uint8Array(vsp.FS.readFile(f.path))
    f.writing = true
    void deliver(f, bytes).finally(() => (f.writing = false))
  }
}

async function deliver(f: LinkedFile, bytes: Uint8Array<ArrayBuffer>) {
  if (f.handle) {
    try {
      const h = f.handle as PermissionHandle
      if ((await h.queryPermission?.({ mode: 'readwrite' })) !== 'granted') await h.requestPermission?.({ mode: 'readwrite' })
      const out = await f.handle.createWritable()
      await out.write(bytes)
      await out.close()
      return
    } catch (err) {
      // Not allowed to write to it (the user declined): hand it over as a download instead.
      console.warn(`WebVSP: couldn't save to ${f.name}; downloading it instead`, err)
    }
  }
  const a = document.createElement('a')
  a.href = URL.createObjectURL(new Blob([bytes]))
  a.download = f.name
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
