export type ParmType = 'double' | 'int' | 'bool' | 'fraction'

export interface ChoiceOption {
  value: number
  label: string
}

export interface ParmInfo {
  id: string
  name: string
  group: string
  displayGroup: string
  descript: string
  type: ParmType
  min: number | null
  max: number | null
  value: number
  /** False when OpenVSP drives this Parm itself; edits to it have no lasting effect. */
  active: boolean
  choice?: { kind: 'choice' | 'flags'; options: ChoiceOption[] }
}

export interface ParmValues {
  values: Record<string, number>
  inactive: string[]
}

export interface ParmGroup {
  name: string
  parms: ParmInfo[]
}

export interface XSecInfo {
  id: string
  surf: number
  surfId: string
  index: number
  shape: number
  groups: ParmGroup[]
}

export interface GeomSchema {
  geomId: string
  groups: ParmGroup[]
  xsecs: XSecInfo[]
}

export interface GeomNode {
  id: string
  name: string
  type: string
  parent: string | null
  children: string[]
  shown: boolean
  /** GEOM_DRAW_WIRE=0, HIDDEN=1, SHADE=2, TEXTURE=3, NONE=4 */
  drawType: number
  color: [number, number, number]
  numXSecSurfs: number
}

export interface UndoState {
  canUndo: boolean
  canRedo: boolean
  undoLabel: string | null
  redoLabel: string | null
}

export interface ModelTree {
  geoms: GeomNode[]
  /** Geoms selected in the GUI */
  active: string[]
  vehicleId: string
  fileName: string
  version: string
  rev: number
  dirty: boolean
  undo: UndoState
}

export interface ModelChangedEvent {
  type: 'model_changed'
  rev: number
  tree: boolean
  changed: string[]
  removed: string[]
  label: string | null
  undo: UndoState
}

export interface SavedEvent {
  type: 'saved'
  fileName: string
}

export type ServerEvent = ModelChangedEvent | SavedEvent

export interface BBox {
  min: [number, number, number]
  max: [number, number, number]
}

export interface DirListing {
  path: string
  parent: string | null
  home: string
  entries: { name: string; path: string; dir: boolean }[]
}
