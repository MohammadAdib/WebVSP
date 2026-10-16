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
 *   uint32   number of surface parts (Mesh / NGon Mesh tags; 0, or the number of surfaces)
 *   per part, for the surface of the same index:
 *     float32  rgb[3]       the tag's colour; r < 0 means none (drawn like any surface)
 *     uint32   number of edge points
 *     float32  edges[n*3]   the tag's polygon edges as segment pairs (NGon faces), drawn
 *                           instead of the triangles' edges; none: the triangles' edges
 */

export interface SurfaceMesh {
  /** Grid dimensions; nw = 0 means a plain triangle list of nu vertices (Mesh Geoms). */
  nu: number
  nw: number
  positions: Float32Array
  normals: Float32Array
  indices: Uint32Array
  /** The colour the desktop draws this surface in (a Mesh / NGon Mesh tag), as 0..1 RGB */
  color?: [number, number, number]
  /** Polygon edges to draw instead of the triangles' (NGon faces), as segment end points */
  edges?: Float32Array
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
    off += 4 + nf * 12
  }
  if (off + 4 <= buf.byteLength) {
    const np = view.getUint32(off, true)
    off += 4
    for (let s = 0; s < np && s < out.length; s++) {
      const rgb = new Float32Array(buf, off, 3)
      if (rgb[0] >= 0) out[s].color = [rgb[0], rgb[1], rgb[2]]
      const ne = view.getUint32(off + 12, true)
      if (ne > 0) out[s].edges = new Float32Array(buf, off + 16, ne * 3)
      off += 16 + ne * 12
    }
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
