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
