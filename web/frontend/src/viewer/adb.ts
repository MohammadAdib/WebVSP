/**
 * Reader for VSPAERO's aerothermal database (<model>.adb) and its case list (<model>.adb.cases),
 * the files VSPAERO's desktop viewer (src/vsp_aero/Viewer) displays.
 *
 * The .adb is a raw stream written by the solver (src/vsp_aero/Solver/VSP_Solver.C,
 * WriteOutAerothermalDatabaseHeader / Geometry / Solution): one header, then one geometry
 * block and one solution block per case (sweep point or time step). Blocks vary in length
 * (wakes, unsteady meshes), so cases are found by reading through the file in order. Indices
 * in the file are 1-based.
 */

// As VSP_Solver.C writes and glviewer.C checks them.
const MAGIC_V2 = -123789456
const MAGIC_V3 = -123789456 + 3

export interface AdbSurface {
  index: number
  name: string
  component: number
}

export interface AdbHeader {
  version: 2 | 3
  /** 1 = VLM (vortex lattice; Cp is delta-Cp), 2 = panel */
  modelType: number
  symmetry: boolean
  timeAccurate: boolean
  nLoops: number
  nNodes: number
  nTris: number
  nEdges: number
  sref: number
  cref: number
  bref: number
  cg: [number, number, number]
  surfaces: AdbSurface[]
}

export interface AdbTrail {
  /** Points, flat x,y,z */
  points: Float64Array
  soverb: number
}

export interface AdbCase {
  /** Nodes, flat x,y,z */
  nodes: Float32Array
  /** Triangles, flat 0-based node indices */
  tris: Uint32Array
  surfaceId: Int32Array
  component: Int32Array
  minValidStep: Int32Array
  area: Float32Array
  mach: number
  /** Degrees */
  alpha: number
  beta: number
  cpMin: number
  cpMax: number
  /** Per triangle */
  cp: Float32Array
  cpUnsteady: Float32Array
  gamma: Float32Array
  trails: AdbTrail[]
}

export interface AdbCaseInfo {
  mach: number
  alpha: number
  beta: number
  comment: string
}

class Reader {
  private view: DataView
  private le: boolean
  pos: number
  constructor(buf: ArrayBuffer, le: boolean, pos = 0) {
    this.view = new DataView(buf)
    this.le = le
    this.pos = pos
  }
  i32() {
    const v = this.view.getInt32(this.pos, this.le)
    this.pos += 4
    return v
  }
  f32() {
    const v = this.view.getFloat32(this.pos, this.le)
    this.pos += 4
    return v
  }
  f64() {
    const v = this.view.getFloat64(this.pos, this.le)
    this.pos += 8
    return v
  }
  skip(bytes: number) {
    this.pos += bytes
    if (this.pos > this.view.byteLength) throw new Error('VSPAERO results file is truncated')
  }
  chars(n: number) {
    const bytes = new Uint8Array(this.view.buffer, this.pos, n)
    this.pos += n
    const end = bytes.indexOf(0)
    return new TextDecoder().decode(end < 0 ? bytes : bytes.subarray(0, end)).trim()
  }
  get eof() {
    return this.pos >= this.view.byteLength
  }
}

const TO_DEG = 180 / Math.PI

export class AdbFile {
  readonly header: AdbHeader
  private readonly le: boolean
  /** File offsets where each case starts (found as the file is read). */
  private offsets: number[] = []
  private complete = false
  private cache = new Map<number, AdbCase>()
  private readonly buf: ArrayBuffer

  constructor(buf: ArrayBuffer) {
    this.buf = buf
    const probe = new DataView(buf)
    const magic = probe.getInt32(0, true)
    if (magic === MAGIC_V2 || magic === MAGIC_V3) this.le = true
    else if (probe.getInt32(0, false) === MAGIC_V2 || probe.getInt32(0, false) === MAGIC_V3) this.le = false
    else throw new Error('Not a VSPAERO results (.adb) file')

    const r = new Reader(buf, this.le)
    const m = r.i32()
    const h: AdbHeader = {
      version: m === MAGIC_V3 ? 3 : 2,
      modelType: r.i32(),
      symmetry: r.i32() !== 0,
      timeAccurate: r.i32() !== 0,
      nLoops: r.i32(),
      nNodes: r.i32(),
      nTris: r.i32(),
      nEdges: r.i32(),
      sref: r.f32(),
      cref: r.f32(),
      bref: r.f32(),
      cg: [r.f32(), r.f32(), r.f32()],
      surfaces: [],
    }
    const nSurf = r.i32()
    for (let i = 0; i < nSurf; i++) h.surfaces.push({ index: r.i32(), name: r.chars(100), component: r.i32() })
    this.header = h
    this.offsets.push(r.pos)
  }

  /** Number of cases (reads through the whole file once). */
  get caseCount(): number {
    while (!this.complete) this.findNext()
    return this.offsets.length - 1
  }

  /** Case k (0-based). */
  getCase(k: number): AdbCase {
    const hit = this.cache.get(k)
    if (hit) return hit
    while (this.offsets.length <= k + 1 && !this.complete) this.findNext()
    if (k >= this.offsets.length - 1) throw new Error(`No case ${k + 1} in the results file`)
    const c = this.read(this.offsets[k], false)!
    if (this.cache.size > 8) this.cache.delete(this.cache.keys().next().value!)
    this.cache.set(k, c)
    return c
  }

  /** offsets: the start of each case found so far, then where the next one would start. */
  private findNext() {
    const start = this.offsets[this.offsets.length - 1]
    if (start >= this.buf.byteLength) {
      this.complete = true
      return
    }
    const r = new Reader(this.buf, this.le, start)
    this.read(start, true, r)
    this.offsets.push(r.pos)
    if (r.eof) this.complete = true
  }

  /** Read (or just skip, to find the next case) the geometry + solution blocks at pos. */
  private read(pos: number, skipOnly: boolean, reader?: Reader): AdbCase | null {
    const r = reader ?? new Reader(this.buf, this.le, pos)
    const h = this.header

    // ---- geometry
    const nT = h.nTris
    const tris = skipOnly ? null : new Uint32Array(nT * 3)
    const component = skipOnly ? null : new Int32Array(nT)
    const surfaceId = skipOnly ? null : new Int32Array(nT)
    const minValidStep = skipOnly ? null : new Int32Array(nT)
    const area = skipOnly ? null : new Float32Array(nT)
    if (skipOnly) r.skip(nT * 28)
    else {
      for (let t = 0; t < nT; t++) {
        tris![3 * t] = r.i32() - 1
        tris![3 * t + 1] = r.i32() - 1
        tris![3 * t + 2] = r.i32() - 1
        component![t] = r.i32()
        surfaceId![t] = r.i32()
        minValidStep![t] = r.i32()
        area![t] = r.f32()
      }
    }
    const nodes = skipOnly ? null : new Float32Array(h.nNodes * 3)
    if (skipOnly) r.skip(h.nNodes * 12)
    else for (let i = 0; i < h.nNodes * 3; i++) nodes![i] = r.f32()

    const nRotors = r.i32()
    const nNozzles = h.version === 3 ? r.i32() : 0
    r.skip(nRotors * 11 * 8 + nNozzles * 7 * 8)

    const levels = r.i32()
    for (let l = 0; l < levels; l++) {
      const nn = r.i32()
      const ne = r.i32()
      r.skip(nn * 12 + ne * 16)
    }
    r.skip(r.i32() * 4) // Kutta TE edges
    r.skip(r.i32() * 4) // Kutta nodes
    const nCS = r.i32()
    for (let c = 0; c < nCS; c++) {
      r.skip(r.i32() * 12) // control surface nodes
      r.skip(9 * 4) // hinge points and vector
      r.skip(r.i32() * 4) // loops
    }

    // ---- solution
    const mach = r.f32()
    const alpha = r.f32() * TO_DEG
    const beta = r.f32() * TO_DEG
    const cpMin = r.f32()
    const cpMax = r.f32()
    r.skip(h.nLoops * 16 + h.nEdges * 24 + h.nLoops * 24) // computational-grid data
    const cp = skipOnly ? null : new Float32Array(nT)
    const cpUnsteady = skipOnly ? null : new Float32Array(nT)
    const gamma = skipOnly ? null : new Float32Array(nT)
    if (skipOnly) r.skip(nT * 12)
    else {
      for (let t = 0; t < nT; t++) {
        cp![t] = r.f32()
        cpUnsteady![t] = r.f32()
        gamma![t] = r.f32()
      }
    }
    const nTrail = r.i32()
    const trails: AdbTrail[] = []
    for (let i = 0; i < nTrail; i++) {
      r.i32() // TE node
      const soverb = r.f64()
      const n = r.i32()
      if (skipOnly) r.skip(n * 24)
      else {
        const points = new Float64Array(n * 3)
        for (let j = 0; j < n * 3; j++) points[j] = r.f64()
        trails.push({ points, soverb })
      }
    }
    r.skip(nCS * 4) // control surface deflections

    if (skipOnly) return null
    return {
      nodes: nodes!,
      tris: tris!,
      surfaceId: surfaceId!,
      component: component!,
      minValidStep: minValidStep!,
      area: area!,
      mach,
      alpha,
      beta,
      cpMin,
      cpMax,
      cp: cp!,
      cpUnsteady: cpUnsteady!,
      gamma: gamma!,
      trails,
    }
  }
}

/** <model>.adb.cases: one line per case, "mach alpha beta comment" (angles in degrees). */
export function parseCases(text: string): AdbCaseInfo[] {
  const out: AdbCaseInfo[] = []
  for (const line of text.split(/\r?\n/)) {
    const m = line.match(/^\s*(\S+)\s+(\S+)\s+(\S+)\s*(.*)$/)
    if (!m) continue
    const [mach, alpha, beta] = [Number(m[1]), Number(m[2]), Number(m[3])]
    if ([mach, alpha, beta].some((v) => Number.isNaN(v))) continue
    out.push({ mach, alpha, beta, comment: m[4].trim() })
  }
  return out
}

/** <model>.polar: VSPAERO's force and moment coefficients, one row per case (vspaero.C). */
export interface Polar {
  columns: string[]
  rows: number[][]
}

export function parsePolar(text: string): Polar | null {
  const lines = text.split(/\r?\n/)
  const head = lines.findIndex((l) => /\bBeta\b/.test(l) && /\bMach\b/.test(l) && /\bAoA\b/.test(l))
  if (head < 0) return null
  const columns = lines[head].trim().split(/\s+/)
  const rows: number[][] = []
  for (const line of lines.slice(head + 1)) {
    const vals = line.trim().split(/\s+/).map(Number)
    if (vals.length >= 4 && vals.every((v) => Number.isFinite(v))) rows.push(vals)
  }
  return { columns, rows }
}

/** The polar row for a case: same Mach, angle of attack and sideslip (degrees). */
export function polarRow(polar: Polar, mach: number, alpha: number, beta: number): Record<string, number> | null {
  const col = (name: string) => polar.columns.indexOf(name)
  const [ib, im, ia] = [col('Beta'), col('Mach'), col('AoA')]
  const close = (a: number, b: number) => Math.abs(a - b) <= 1e-3 * Math.max(1, Math.abs(b))
  const row = polar.rows.find((r) => close(r[im], mach) && close(r[ia], alpha) && close(r[ib], beta))
  if (!row) return null
  return Object.fromEntries(polar.columns.map((c, i) => [c, row[i]]))
}
