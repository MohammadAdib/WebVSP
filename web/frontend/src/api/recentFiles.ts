import { create } from 'zustand'
import { fileNote } from './fileNote'
import { openDb } from './idb'

/**
 * File > Open Recent: the last RECENT_MAX models opened, or saved under a new name, newest first,
 * kept across visits (IndexedDB, idb.ts; one list per site).
 *
 * Chrome and Edge keep each file's handle: a recent file is read from the disk again (as it is
 * now), and File > Save goes back to it. After a reload the browser asks once to allow it.
 * Browsers without file handles keep the file's contents instead, as last opened or saved here.
 */
export const RECENT_MAX = 10

export interface RecentFile {
  name: string
  handle: FileSystemFileHandle | null
  /** The file's contents, in browsers without file handles */
  data: Blob | null
}

export const useRecent = create<{ list: RecentFile[] }>(() => ({ list: [] }))

const STORE = 'recentFiles'
/** This site's list: the legacy site (/legacy) keeps its own. */
const KEY = 'list:' + new URL(import.meta.env.BASE_URL, window.location.href).pathname

// Changes one at a time, after the list from earlier visits is in.
let queue: Promise<unknown> = (async () => {
  try {
    const db = await openDb()
    const list = await new Promise<unknown>((resolve, reject) => {
      const req = db.transaction(STORE).objectStore(STORE).get(KEY)
      req.onsuccess = () => resolve(req.result)
      req.onerror = () => reject(req.error)
    })
    db.close()
    if (Array.isArray(list)) useRecent.setState({ list: (list as RecentFile[]).slice(0, RECENT_MAX) })
  } catch (err) {
    console.warn('WebVSP: no recent files from earlier visits', err)
  }
})()

function change(fn: (list: RecentFile[]) => Promise<RecentFile[] | null>) {
  queue = queue
    .then(async () => {
      const list = await fn(useRecent.getState().list)
      if (list) await store(list)
    })
    .catch((err) => console.warn('WebVSP: recent files', err))
  return queue
}

async function store(list: RecentFile[]) {
  useRecent.setState({ list })
  try {
    const db = await openDb()
    await new Promise<void>((resolve, reject) => {
      const tx = db.transaction(STORE, 'readwrite')
      tx.objectStore(STORE).put(list, KEY)
      tx.oncomplete = () => resolve()
      tx.onerror = () => reject(tx.error)
      tx.onabort = () => reject(tx.error)
    })
    db.close()
  } catch (err) {
    console.warn('WebVSP: the recent files are only remembered for this visit', err)
  }
}

/** Whether r is that file: the same file on disk, or (without handles) the same name. */
async function isFile(r: RecentFile, name: string, handle: FileSystemFileHandle | null) {
  if (r.handle && handle) return r.handle.isSameEntry(handle).catch(() => false)
  return !r.handle && !handle && r.name === name
}

async function without(list: RecentFile[], name: string, handle: FileSystemFileHandle | null) {
  const out: RecentFile[] = []
  for (const r of list) if (!(await isFile(r, name, handle))) out.push(r)
  return out
}

/** A model just opened, or saved under a new name: to the top of the list. Without a handle,
 *  its contents are kept. */
export function addRecent(name: string, handle: FileSystemFileHandle | null, data: Uint8Array<ArrayBuffer> | null) {
  if (!handle && !data) return
  const entry: RecentFile = { name, handle, data: handle ? null : new Blob([data!]) }
  return change(async (list) => [entry, ...(await without(list, name, handle))].slice(0, RECENT_MAX))
}

/** A file saved again (browsers without handles): its kept contents follow, its place stays. */
export function updateRecent(name: string, data: Uint8Array<ArrayBuffer>) {
  return change(async (list) => {
    if (!list.some((r) => !r.handle && r.name === name)) return null
    return list.map((r) => (!r.handle && r.name === name ? { ...r, data: new Blob([data]) } : r))
  })
}

export function clearRecent() {
  return change(async () => [])
}

type PermissionHandle = FileSystemFileHandle & {
  queryPermission?: (o: { mode: 'read' | 'readwrite' }) => Promise<PermissionState>
  requestPermission?: (o: { mode: 'read' | 'readwrite' }) => Promise<PermissionState>
}

/** A recent file's name, contents and handle, to open; null (with a note why) if it can't be.
 *  Must start from a click: the browser may ask to allow the file. */
export async function readRecent(r: RecentFile) {
  try {
    if (r.handle) {
      // Allowed to edit it too, as File > Open asks, so File > Save writes it back.
      const h = r.handle as PermissionHandle
      let allowed = (await h.queryPermission?.({ mode: 'readwrite' })) ?? 'granted'
      if (allowed !== 'granted') allowed = (await h.requestPermission?.({ mode: 'readwrite' })) ?? 'denied'
      if (allowed !== 'granted' && (await h.queryPermission?.({ mode: 'read' })) !== 'granted') {
        fileNote(`Not allowed to open ${r.name}`, { failed: true })
        return null
      }
      const file = await h.getFile()
      return { name: file.name, handle: r.handle, data: new Uint8Array(await file.arrayBuffer()) }
    }
    if (r.data) return { name: r.name, handle: null, data: new Uint8Array(await r.data.arrayBuffer()) }
  } catch (err) {
    if (err instanceof DOMException && err.name === 'NotFoundError') {
      void change(async (list) => without(list, r.name, r.handle))
      fileNote(`${r.name} isn't there any more (moved or deleted)`, { failed: true })
      return null
    }
    console.warn(`WebVSP: couldn't open ${r.name}`, err)
  }
  fileNote(`Couldn't open ${r.name}`, { failed: true })
  return null
}
