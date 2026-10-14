/**
 * Host-specific features. The browser build uses the in-app file browser (backed by
 * the local server); a desktop shell (e.g. Tauri) can swap in native dialogs here
 * without touching the rest of the UI.
 */
import { useStore } from './store'

export function pickOpenFile(title = 'Open', filter = '*.vsp3', dir?: string): Promise<string | null> {
  return new Promise((resolve) => useStore.getState().setFileDialog({ mode: 'open', title, filter, dir, resolve }))
}

export function pickSaveFile(title = 'Save As', defaultName = '', filter = '*.vsp3', dir?: string): Promise<string | null> {
  return new Promise((resolve) =>
    useStore.getState().setFileDialog({ mode: 'save', title, defaultName, filter, dir, resolve }),
  )
}

export function setWindowTitle(title: string) {
  document.title = title
}
