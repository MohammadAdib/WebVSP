import { create } from 'zustand'
import type { MDialog, MirrorEvent, MirrorMsg, MWidget, MWindow } from './types'

interface MirrorState {
  enabled: boolean
  windows: Record<number, MWindow>
  widgets: Record<number, MWidget>
  /** children ids per "win:parent", in child order */
  children: Record<string, number[]>
  dialogs: MDialog[]
  send: (e: MirrorEvent) => void

  setSender: (fn: (e: MirrorEvent) => void) => void
  snapshot: (s: { enabled: boolean; windows: MWindow[]; widgets: MWidget[]; dialogs: MDialog[] }) => void
  apply: (msgs: MirrorMsg[]) => void
  dialogDone: (seq: number) => void
  finishClose: (id: number) => void
}

/** How long a closed window stays on screen to animate out (keep in sync with CSS). */
export const CLOSE_MS = 180

export const childKey = (win: number, parent: number) => `${win}:${parent}`

function buildChildren(widgets: Record<number, MWidget>) {
  const children: Record<string, number[]> = {}
  for (const w of Object.values(widgets)) {
    ;(children[childKey(w.win, w.p)] ??= []).push(w.id)
  }
  for (const list of Object.values(children)) list.sort((a, b) => widgets[a].o - widgets[b].o)
  return children
}

export const useMirror = create<MirrorState>((set, get) => ({
  enabled: false,
  windows: {},
  widgets: {},
  children: {},
  dialogs: [],
  send: () => {},

  setSender: (send) => set({ send }),

  snapshot: (s) => {
    const windows = Object.fromEntries(s.windows.map((w) => [w.id, w]))
    const widgets = Object.fromEntries(s.widgets.map((w) => [w.id, w]))
    set({ enabled: s.enabled, windows, widgets, children: buildChildren(widgets), dialogs: s.dialogs })
  },

  apply: (msgs) => {
    let { windows, widgets, children, dialogs } = get()
    let structural = false
    for (const m of msgs) {
      if (m.t === 'dialog') {
        dialogs = [...dialogs, m]
        continue
      }
      if (m.full) {
        windows = {}
        widgets = {}
        structural = true
      } else {
        windows = { ...windows }
        widgets = { ...widgets }
      }
      for (const id of m.closed) {
        // Keep it (and its widgets) briefly so it can animate out; finishClose removes it.
        if (windows[id] && !m.full) {
          windows[id] = { ...windows[id], closing: true }
          setTimeout(() => get().finishClose(id), CLOSE_MS)
        }
      }
      for (const w of m.windows) windows[w.id] = w
      for (const w of m.widgets) {
        const old = widgets[w.id]
        if (!old || old.p !== w.p || old.o !== w.o || old.win !== w.win) structural = true
        widgets[w.id] = w
      }
      for (const id of m.removed) {
        // A closing window's widgets stay until its exit animation ends (finishClose).
        if (widgets[id] && !windows[widgets[id].win]?.closing) {
          delete widgets[id]
          structural = true
        }
      }
    }
    if (structural) children = buildChildren(widgets)
    set({ windows, widgets, children, dialogs })
  },

  dialogDone: (seq) => set({ dialogs: get().dialogs.filter((d) => d.seq !== seq) }),

  finishClose: (id) => {
    const { windows, widgets } = get()
    if (!windows[id]?.closing) return // reopened meanwhile
    const nextWindows = { ...windows }
    delete nextWindows[id]
    const nextWidgets = { ...widgets }
    for (const w of Object.values(widgets)) if (w.win === id) delete nextWidgets[w.id]
    set({ windows: nextWindows, widgets: nextWidgets, children: buildChildren(nextWidgets) })
  },
}))

export const sendMirror = (e: MirrorEvent) => useMirror.getState().send(e)
