import type { BBox, ModelTree, ServerEvent } from './types'
import { wasmConnect, wasmRequest, WasmEngineError } from './wasmEngine'

export class ApiError extends Error {
  status: number
  constructor(status: number, message: string) {
    super(message)
    this.status = status
  }
}

/** A request to the engine (OpenVSP in this tab, see wasmEngine.ts). */
async function request<T>(method: string, path: string, body?: unknown): Promise<T> {
  try {
    return (await wasmRequest(method, path, body)) as T
  } catch (err) {
    if (err instanceof WasmEngineError) throw new ApiError(err.status, err.message)
    throw err
  }
}

const enc = encodeURIComponent

export const api = {
  model: () => request<ModelTree>('GET', '/model'),
  geomTypes: () => request<string[]>('GET', '/geom-types'),
  bbox: () => request<BBox | null>('GET', '/bbox'),
  /** Every geom's bounding box, hidden ones too. */
  bboxAll: () => request<BBox | null>('GET', '/bbox/all'),
  mesh: (id: string) => request<ArrayBuffer>('GET', `/geoms/${enc(id)}/mesh`),
  /** Select a geom as a click in the 3D view does; toggle (a shift click) adds it or takes it out */
  pick: (id: string, toggle = false) => request('POST', '/pick', { id, toggle }),
  /** A geom's origin in the 3D view, and moving geoms by an offset (the move gizmo; one undo) */
  geomOrigin: (id: string) => request<[number, number, number]>('GET', `/geoms/${enc(id)}/origin`),
  moveGeoms: (ids: string[], delta: [number, number, number]) => request('POST', '/geoms/move', { ids, delta }),
  /** Sets geoms' draw type (GEOM_DRAW_WIRE=0, HIDDEN=1, SHADE=2, TEXTURE=3, NONE=4). */
  setDrawType: (ids: string[], type: number) => request('POST', '/geoms/draw-type', { ids, type }),
  /** Rotates each geom about its own origin, about a world axis (degrees). */
  rotateGeoms: (ids: string[], axis: [number, number, number], angle: number) => request('POST', '/geoms/rotate', { ids, axis, angle }),
}

/** Engine messages that aren't model events: GUI mirror output and prompt bookkeeping. */
// eslint-disable-next-line @typescript-eslint/no-explicit-any
export type SocketMessage = ServerEvent | { type: 'mirror_snapshot' | 'mirror' | 'dialog_done'; [k: string]: any }

/** Model events and the mirrored GUI's output, once OpenVSP has started. */
export const connectEvents = wasmConnect
