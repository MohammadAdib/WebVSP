import { create } from 'zustand'
import { api } from './api/client'
import type { GeomNode, ModelTree, ServerEvent, UndoState } from './api/types'

/** Stable empty list: a selector returning a fresh [] each call re-renders forever. */
export const NO_GEOMS: GeomNode[] = []

export type DisplayMode = 'shaded' | 'wire' | 'both'
export type ViewName =
  | 'top' | 'front' | 'left' | 'leftiso' | 'bottom' | 'rear' | 'right' | 'rightiso' | 'fit'
  // relative moves (Blender numpad 2/4/6/8 and 9)
  | 'orbit-left' | 'orbit-right' | 'orbit-up' | 'orbit-down' | 'flip'

export interface FileDialogRequest {
  mode: 'open' | 'save'
  title: string
  defaultName?: string
  /** FLTK-style filter, e.g. "*.vsp3" or "*.{af,dat}" */
  filter?: string
  dir?: string
  resolve: (path: string | null) => void
}

interface State {
  tree: ModelTree | null
  geomsById: Record<string, GeomNode>
  geomTypes: string[]
  selectedId: string | null
  /** Bumped per geom when its mesh must be refetched. */
  meshRev: Record<string, number>
  /** Bumped whenever parm values may have changed. */
  valuesRev: number
  /** Bumped when a geom's parm *structure* may have changed (sections added, etc). */
  schemaRev: number
  connected: boolean
  error: string | null
  lastAction: string | null
  displayMode: DisplayMode
  viewRequest: { name: ViewName; n: number }
  /** Orthographic (true) or perspective camera */
  ortho: boolean
  fileDialog: FileDialogRequest | null
  /** Bounding sphere of the shown model, set when the view is fitted. */
  bounds: { center: [number, number, number]; size: [number, number, number]; radius: number } | null

  loadTree: () => Promise<void>
  select: (id: string | null) => void
  handleEvent: (e: ServerEvent) => void
  run: <T>(fn: () => Promise<T>) => Promise<T | undefined>
  setDisplayMode: (m: DisplayMode) => void
  requestView: (name: ViewName) => void
  toggleOrtho: () => void
  setConnected: (c: boolean) => void
  setFileDialog: (d: FileDialogRequest | null) => void
  setBounds: (b: State['bounds']) => void
  clearError: () => void
}

export const useStore = create<State>((set, get) => ({
  tree: null,
  geomsById: {},
  geomTypes: [],
  selectedId: null,
  meshRev: {},
  valuesRev: 0,
  schemaRev: 0,
  connected: false,
  error: null,
  lastAction: null,
  displayMode: 'both',
  viewRequest: { name: 'fit', n: 0 },
  ortho: (() => {
    try {
      return localStorage.getItem('titanvsp-ortho') === '1'
    } catch {
      return false
    }
  })(),
  fileDialog: null,
  bounds: null,

  loadTree: async () => {
    const [tree, types] = await Promise.all([api.model(), get().geomTypes.length ? null : api.geomTypes()])
    const geomsById = Object.fromEntries(tree.geoms.map((g) => [g.id, g]))
    const sel = get().selectedId
    const prev = get().tree
    // A different file (or the first geoms in an empty model) needs the camera re-fitted;
    // otherwise its clip planes and zoom still frame the previous model.
    const refit = !!prev && (prev.fileName !== tree.fileName || (prev.geoms.length === 0 && tree.geoms.length > 0))
    set({
      tree,
      geomsById,
      ...(types ? { geomTypes: types } : {}),
      selectedId: sel && geomsById[sel] ? sel : null,
    })
    if (refit) get().requestView('fit')
  },

  select: (id) => set({ selectedId: id }),

  handleEvent: (e) => {
    if (e.type === 'saved') {
      const tree = get().tree
      if (tree) set({ tree: { ...tree, fileName: e.fileName, dirty: false } })
      return
    }
    const meshRev = { ...get().meshRev }
    for (const id of e.changed) meshRev[id] = (meshRev[id] ?? 0) + 1
    for (const id of e.removed) delete meshRev[id]
    const tree = get().tree
    set({
      meshRev,
      valuesRev: get().valuesRev + 1,
      // Structural edits (sections, shapes, undo of those) arrive with a label.
      schemaRev: e.label ? get().schemaRev + 1 : get().schemaRev,
      lastAction: e.label ?? get().lastAction,
      tree: tree ? { ...tree, undo: e.undo as UndoState, rev: e.rev, dirty: true } : tree,
    })
    if (e.tree || e.label) void get().loadTree()
  },

  run: async (fn) => {
    try {
      const r = await fn()
      set({ error: null })
      return r
    } catch (err) {
      set({ error: err instanceof Error ? err.message : String(err) })
      return undefined
    }
  },

  setDisplayMode: (displayMode) => set({ displayMode }),
  requestView: (name) => set({ viewRequest: { name, n: get().viewRequest.n + 1 } }),
  toggleOrtho: () => {
    const ortho = !get().ortho
    try {
      localStorage.setItem('titanvsp-ortho', ortho ? '1' : '0')
    } catch {
      /* storage unavailable */
    }
    set({ ortho })
  },
  setConnected: (connected) => set({ connected }),
  setFileDialog: (fileDialog) => set({ fileDialog }),
  setBounds: (bounds) => set({ bounds }),
  clearError: () => set({ error: null }),
}))
