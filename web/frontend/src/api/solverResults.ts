import { create } from 'zustand'

/**
 * VSPAERO's result files saved next to the model on the user's disk, as the desktop leaves them,
 * so Load Previous Results can find them again (File > Open the model together with them).
 *
 * The page only has the model's own file (a file handle), not its folder, so the first save asks
 * for the folder (a button press is needed for the browser's folder picker; it opens where the
 * model is); later runs of that model save there by themselves. Browsers without folder access
 * download the files instead.
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

/** What the toast shows: a question about a finished run's results, or a note about a save. */
export const useResultsSave = create<{ ask: ResultsOffer | null; note: string | null; failed: boolean }>(() => ({
  ask: null,
  note: null,
  failed: false,
}))

type DirHandle = FileSystemDirectoryHandle & {
  queryPermission?: (o: { mode: 'readwrite' }) => Promise<PermissionState>
}
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
  const dir = folders.get(offer.modelPath)
  if (dir && (await dir.queryPermission?.({ mode: 'readwrite' })) === 'granted') return writeAll(dir, offer)
  if (declined.has(offer.modelPath)) return
  useResultsSave.setState({ ask: offer })
}

/** "Save": choose the folder (starting in the model's) and write the files; must run from a click. */
export async function saveOffered() {
  const offer = useResultsSave.getState().ask
  if (!offer) return
  useResultsSave.setState({ ask: null })
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
  await writeAll(dir, offer)
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
