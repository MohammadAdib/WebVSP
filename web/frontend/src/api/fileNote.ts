import { create } from 'zustand'

/**
 * A short note beside the model's file name, at the bottom left of the 3D view: "Saved" as soon
 * as a save (File > Save, Ctrl+S, Save As, any file OpenVSP writes for the user) is on the user's
 * disk, or why a file couldn't be saved or opened. Shown for NOTE_MS (FAILED_NOTE_MS for a
 * failure), fading in and out (`.file-note` in index.css).
 */
export const NOTE_MS = 2000
export const FAILED_NOTE_MS = 6000

export interface FileNote {
  text: string
  /** The file it's about: left out when it's the model's file, which is named beside it */
  file?: string
  failed: boolean
  /** Counts notes, so one just like the last starts its animation again */
  n: number
}

export const useFileNote = create<{ note: FileNote | null }>(() => ({ note: null }))

let timer = 0
export function fileNote(text: string, opts: { file?: string; failed?: boolean } = {}) {
  window.clearTimeout(timer)
  const n = (useFileNote.getState().note?.n ?? 0) + 1
  const failed = !!opts.failed
  useFileNote.setState({ note: { text, file: opts.file, failed, n } })
  timer = window.setTimeout(() => useFileNote.setState({ note: null }), failed ? FAILED_NOTE_MS : NOTE_MS)
}
