/** Decoder for the backend's binary mesh format (see web/backend/vspweb/tess.py). */

export interface SurfaceMesh {
  nu: number
  nw: number
  positions: Float32Array
  normals: Float32Array
  indices: Uint32Array
}

export function decodeMesh(buf: ArrayBuffer): SurfaceMesh[] {
  const view = new DataView(buf)
  const magic = String.fromCharCode(view.getUint8(0), view.getUint8(1), view.getUint8(2), view.getUint8(3))
  if (magic !== 'VSPM') throw new Error('bad mesh payload')
  const n = view.getUint32(4, true)
  let off = 8
  const out: SurfaceMesh[] = []
  for (let s = 0; s < n; s++) {
    const nu = view.getUint32(off, true)
    const nw = view.getUint32(off + 4, true)
    const nidx = view.getUint32(off + 8, true)
    off += 12
    const nv = nu * nw * 3
    const positions = new Float32Array(buf, off, nv)
    off += nv * 4
    const normals = new Float32Array(buf, off, nv)
    off += nv * 4
    const indices = new Uint32Array(buf, off, nidx)
    off += nidx * 4
    out.push({ nu, nw, positions, normals, indices })
  }
  return out
}

/** Line-segment indices along the u/w grid lines (the "wireframe" VSP draws). */
export function gridLineIndices(nu: number, nw: number): Uint32Array {
  const segs: number[] = []
  for (let i = 0; i < nu; i++) {
    for (let j = 0; j < nw - 1; j++) segs.push(i * nw + j, i * nw + j + 1)
  }
  for (let j = 0; j < nw; j++) {
    for (let i = 0; i < nu - 1; i++) segs.push(i * nw + j, (i + 1) * nw + j)
  }
  return new Uint32Array(segs)
}
