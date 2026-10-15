import { create } from 'zustand'
import { AdbFile, parseCases, parsePolar, type AdbCaseInfo, type Polar } from './adb'

/** The VSPAERO results viewer (VSPAERO > Launch Viewer). */
interface ViewerState {
  open: boolean
  name: string
  adb: AdbFile | null
  cases: AdbCaseInfo[]
  /** Coefficients per case, from <model>.polar (if there is one) */
  polar: Polar | null
  error: string | null
  onClose: (() => void) | null
  /** Show results: the .adb bytes, and the .adb.cases and .polar text. */
  show: (name: string, adb: ArrayBuffer, casesText: string, polarText: string, onClose?: () => void) => void
  close: () => void
}

export const useViewer = create<ViewerState>((set, get) => ({
  open: false,
  name: '',
  adb: null,
  cases: [],
  polar: null,
  error: null,
  onClose: null,
  show: (name, adb, casesText, polarText, onClose) => {
    get().onClose?.()
    try {
      set({ open: true, name, adb: new AdbFile(adb), cases: parseCases(casesText), polar: parsePolar(polarText), error: null, onClose: onClose ?? null })
    } catch (err) {
      set({ open: true, name, adb: null, cases: [], polar: null, error: err instanceof Error ? err.message : String(err), onClose: onClose ?? null })
    }
  },
  close: () => {
    get().onClose?.()
    set({ open: false, adb: null, cases: [], polar: null, error: null, onClose: null })
  },
}))
