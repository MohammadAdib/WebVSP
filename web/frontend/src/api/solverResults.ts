import { create } from 'zustand'

/**
 * VSPAERO's result files saved next to the model on the user's disk, as the desktop leaves them,
 * so Load Previous Results can find them again (File > Open the model together with them).
 *
 * The page only has the model's own file (a file handle), not its folder, so the first save asks
 * for the folder (a button press is needed for the browser's folder picker; it opens where the
 * model is); later runs of that model save there by themselves. The folder is remembered across
 * visits (IndexedDB, paired with the model's file); after a reload the browser usually wants a
 * click to allow writing to it again, so the first run asks once more, without the picker.
 * Browsers without folder access download the files instead.
 */

export interface ResultFile {
  name: string
  data: Uint8Array<ArrayBuffer>
}

export interface ResultsOffer {
  /** The model in the module's file system, its name, and its file on disk (Chrome/Edge) */
  modelPath: string
  modelName: string
  modelHandle: FileSystemFileHandle | null
  files: ResultFile[]
}

type DirHandle = FileSystemDirectoryHandle & {
  queryPermission?: (o: { mode: 'readwrite' }) => Promise<PermissionState>
  requestPermission?: (o: { mode: 'readwrite' }) => Promise<PermissionState>
}

/** What the toast shows: a question about a finished run's results (with the folder chosen for
 *  them before, if any, waiting for permission), or a note about a save. */
export const useResultsSave = create<{
  ask: (ResultsOffer & { folder?: DirHandle }) | null
  note: string | null
  failed: boolean
}>(() => ({ ask: null, note: null, failed: false }))
type DirPicker = (o: { id?: string; mode?: 'readwrite'; startIn?: FileSystemHandle | string }) => Promise<DirHandle>

/** The folder chosen for each model's results (this session), and models whose results the
 *  user chose not to save. */
const folders = new Map<string, DirHandle>()
const declined = new Set<string>()

let noteTimer = 0
function note(text: string, failed = false) {
  window.clearTimeout(noteTimer)
  useResultsSave.setState({ note: text, failed })
  noteTimer = window.setTimeout(() => useResultsSave.setState({ note: null }), failed ? 9000 : 5000)
}

/** A finished run's results: saved at once where this model's went before, otherwise asked about. */
export async function offerResults(offer: ResultsOffer) {
  if (!offer.files.length) return
  const dir = folders.get(offer.modelPath) ?? (await rememberedFolder(offer.modelHandle))
  if (dir) folders.set(offer.modelPath, dir)
  if (dir && (await permission(dir)) === 'granted') return writeAll(dir, offer)
  if (declined.has(offer.modelPath)) return
  useResultsSave.setState({ ask: { ...offer, folder: dir } })
}

/** "Save": allow the folder chosen before, or choose one (starting in the model's), then write the
 *  files; must run from a click. */
export async function saveOffered() {
  const offer = useResultsSave.getState().ask
  if (!offer) return
  useResultsSave.setState({ ask: null })
  if (offer.folder) {
    try {
      if ((await offer.folder.requestPermission?.({ mode: 'readwrite' })) === 'granted') return writeAll(offer.folder, offer)
    } catch {
      /* gone or refused: choose again */
    }
  }
  const pick = (window as unknown as { showDirectoryPicker?: DirPicker }).showDirectoryPicker
  if (!pick) {
    for (const f of offer.files) download(f)
    return note(`Downloaded ${offer.files.length} VSPAERO result files`)
  }
  let dir: DirHandle
  try {
    dir = await pick({ id: 'webvsp-results', mode: 'readwrite', startIn: offer.modelHandle ?? 'documents' })
  } catch {
    return // cancelled
  }
  folders.set(offer.modelPath, dir)
  if (offer.modelHandle) void remember(offer.modelHandle, dir)
  await writeAll(dir, offer)
}

async function permission(dir: DirHandle) {
  try {
    return await dir.queryPermission?.({ mode: 'readwrite' })
  } catch {
    return undefined
  }
}

// ------------------------------------------------------------------ remembered folders

/** Each model's results folder, kept across visits: { model: its file, folder } records in
 *  IndexedDB (file and folder handles can be stored there). Matched by isSameEntry, so two models
 *  with the same name in different folders keep their own. Any failure: just this session. */
const DB = 'webvsp'
const STORE = 'resultFolders'
interface FolderRecord {
  model: FileSystemFileHandle
  folder: DirHandle
}

function openDb(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open(DB, 1)
    req.onupgradeneeded = () => req.result.createObjectStore(STORE, { autoIncrement: true })
    req.onsuccess = () => resolve(req.result)
    req.onerror = () => reject(req.error)
  })
}

/** All records, with their keys. */
async function records(db: IDBDatabase): Promise<{ key: IDBValidKey; rec: FolderRecord }[]> {
  return new Promise((resolve, reject) => {
    const out: { key: IDBValidKey; rec: FolderRecord }[] = []
    const req = db.transaction(STORE).objectStore(STORE).openCursor()
    req.onsuccess = () => {
      const c = req.result
      if (!c) return resolve(out)
      out.push({ key: c.key, rec: c.value as FolderRecord })
      c.continue()
    }
    req.onerror = () => reject(req.error)
  })
}

async function findRecord(db: IDBDatabase, model: FileSystemFileHandle) {
  for (const r of await records(db)) {
    if (await r.rec.model.isSameEntry(model).catch(() => false)) return r
  }
  return undefined
}

async function rememberedFolder(model: FileSystemFileHandle | null): Promise<DirHandle | undefined> {
  if (!model) return undefined
  try {
    const db = await openDb()
    const found = await findRecord(db, model)
    db.close()
    return found?.rec.folder
  } catch {
    return undefined
  }
}

async function remember(model: FileSystemFileHandle, folder: DirHandle) {
  try {
    const db = await openDb()
    const found = await findRecord(db, model)
    await new Promise<void>((resolve, reject) => {
      const tx = db.transaction(STORE, 'readwrite')
      const store = tx.objectStore(STORE)
      if (found) store.put({ model, folder }, found.key)
      else store.add({ model, folder })
      tx.oncomplete = () => resolve()
      tx.onerror = () => reject(tx.error)
    })
    db.close()
  } catch (err) {
    console.warn('WebVSP: the results folder is only remembered for this visit', err)
  }
}

/** "Not now": not asked again for this model this session. */
export function declineOffered() {
  const offer = useResultsSave.getState().ask
  if (offer) declined.add(offer.modelPath)
  useResultsSave.setState({ ask: null })
}

async function writeAll(dir: DirHandle, offer: ResultsOffer) {
  try {
    for (const f of offer.files) {
      const out = await (await dir.getFileHandle(f.name, { create: true })).createWritable()
      await out.write(f.data)
      await out.close()
    }
    // Next to the model, or in some other folder the user chose.
    const beside = await dir
      .getFileHandle(offer.modelName)
      .then((h) => (offer.modelHandle ? h.isSameEntry(offer.modelHandle) : true))
      .catch(() => false)
    note(`Saved ${offer.files.length} VSPAERO result files ${beside ? `next to ${offer.modelName}` : `in ${dir.name}`}`)
  } catch (err) {
    console.warn('WebVSP: saving the VSPAERO results failed', err)
    folders.delete(offer.modelPath)
    note(`Couldn't save the VSPAERO results in ${dir.name}`, true)
  }
}

function download(f: ResultFile) {
  const a = document.createElement('a')
  a.href = URL.createObjectURL(new Blob([f.data]))
  a.download = f.name
  a.click()
  setTimeout(() => URL.revokeObjectURL(a.href), 10000)
}
