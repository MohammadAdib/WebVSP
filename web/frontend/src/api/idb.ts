/**
 * WebVSP's IndexedDB database, `webvsp`: what the page keeps across visits, file and folder
 * handles included (they can be stored here, not in localStorage). Both sites (`/` and
 * `/legacy`) are on one origin, so they share it. Its object stores:
 *
 *   - `resultFolders`: each model's VSPAERO results folder (solverResults.ts), auto-numbered;
 *   - `recentFiles`: File > Open Recent's list, one per site (recentFiles.ts).
 *
 * A new store needs VERSION raised and creating in `onupgradeneeded`, which runs for any older
 * version. Every user opens it here, so all ask for the same version.
 */
const NAME = 'webvsp'
const VERSION = 2

export function openDb(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open(NAME, VERSION)
    req.onupgradeneeded = () => {
      const db = req.result
      if (!db.objectStoreNames.contains('resultFolders')) db.createObjectStore('resultFolders', { autoIncrement: true })
      if (!db.objectStoreNames.contains('recentFiles')) db.createObjectStore('recentFiles')
    }
    req.onsuccess = () => {
      // Closed if a newer page (another tab) needs to upgrade it.
      req.result.onversionchange = () => req.result.close()
      resolve(req.result)
    }
    req.onerror = () => reject(req.error)
  })
}
