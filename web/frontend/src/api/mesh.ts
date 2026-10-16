/**
 * Decoder for the engine's binary mesh payload (MeshBytes in web/wasm/engine/engine.cpp).
 * Little-endian, every field 4-byte aligned:
 *
 *   char[4]  "VSPM"
 *   uint32   number of surfaces
 *   per surface:
 *     uint32   nu, nw       grid dimensions (vertex k = i*nw + j); nw = 0 means a plain
 *                           triangle list of nu vertices (Mesh Geoms have no u/w grid)
 *     uint32   nidx         number of triangle indices
 *     float32  positions[nv*3], normals[nv*3]   (nv = nu*nw, or nu when nw = 0)
 *     uint32   indices[nidx]
 *   uint32   number of outline points (OpenVSP's feature lines, MirrorFeatureLines)
 *   float32  outline[n*3]   pairs of points, one pair per line segment
 */

export interface SurfaceMesh {
  /** Grid dimensions; nw = 0 means a plain triangle list of nu vertices (Mesh Geoms). */
  nu: number
  nw: number
  positions: Float32Array
  normals: Float32Array
  indices: Uint32Array
}

/** A geom's surfaces, and its outline (feature lines) as segment end points. */
export function decodeMesh(buf: ArrayBuffer): SurfaceMesh[] & { features?: Float32Array } {
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
    const nv = (nw === 0 ? nu : nu * nw) * 3
    const positions = new Float32Array(buf, off, nv)
    off += nv * 4
    const normals = new Float32Array(buf, off, nv)
    off += nv * 4
    const indices = new Uint32Array(buf, off, nidx)
    off += nidx * 4
    out.push({ nu, nw, positions, normals, indices })
  }
  const res: SurfaceMesh[] & { features?: Float32Array } = out
  if (off + 4 <= buf.byteLength) {
    const nf = view.getUint32(off, true)
    res.features = new Float32Array(buf, off + 4, nf * 3)
  }
  return res
}

/** Line-segment indices along the u/w grid lines (the "wireframe" VSP draws). */
export function gridLineIndices(nu: number, nw: number): Uint32Array {
  if (nw === 0) return triangleEdgeIndices(nu)
  const segs: number[] = []
  for (let i = 0; i < nu; i++) {
    for (let j = 0; j < nw - 1; j++) segs.push(i * nw + j, i * nw + j + 1)
  }
  for (let j = 0; j < nw; j++) {
    for (let i = 0; i < nu - 1; i++) segs.push(i * nw + j, (i + 1) * nw + j)
  }
  return new Uint32Array(segs)
}

/** Line-segment indices along every triangle edge of a triangle list of n vertices. */
function triangleEdgeIndices(n: number): Uint32Array {
  const segs = new Uint32Array((n / 3) * 6)
  for (let t = 0, k = 0; t + 2 < n; t += 3) {
    segs[k++] = t
    segs[k++] = t + 1
    segs[k++] = t + 1
    segs[k++] = t + 2
    segs[k++] = t + 2
    segs[k++] = t
  }
  return segs
}
