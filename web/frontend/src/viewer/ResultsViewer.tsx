import { Canvas } from '@react-three/fiber'
import { useCallback, useEffect, useMemo, useRef, useState, type ReactNode } from 'react'
import * as THREE from 'three'
import { isTopZ, nextZ } from '../mirror/Window'
import { useStore, type ViewName } from '../store'
import { Navigation, useViewKeys, ViewTools, type Box, type ViewRequest } from '../viewport/camera'
import { polarRow, type AdbCase, type AdbFile } from './adb'
import { useViewer } from './store'

/**
 * VSPAERO's results viewer, in the page: the surfaces coloured by pressure or vorticity per
 * solution case, with the trailing wakes; the core of VSPAERO's desktop viewer
 * (src/vsp_aero/Viewer), reading the same files. Navigation is the main 3D view's.
 */

type Quantity = 'cp' | 'cpSteady' | 'cpUnsteady' | 'gamma' | 'shaded'

// As the desktop viewer titles its legend.
const QUANTITIES: { key: Quantity; label: (vlm: boolean) => string }[] = [
  { key: 'cp', label: (vlm) => (vlm ? 'Delta-Cp' : 'Cp') },
  { key: 'cpSteady', label: (vlm) => (vlm ? 'Steady Delta-Cp' : 'Steady Cp') },
  { key: 'cpUnsteady', label: (vlm) => (vlm ? 'Unsteady Delta-Cp' : 'Unsteady Cp') },
  { key: 'gamma', label: () => 'Vorticity' },
  { key: 'shaded', label: () => 'Shaded' },
]

// The desktop viewer's colour ramp: blue, cyan, green, yellow, red, magenta.
const RAMP: [number, number, number][] = [
  [0, 0, 1],
  [0, 1, 1],
  [0, 1, 0],
  [1, 1, 0],
  [1, 0, 0],
  [1, 0, 1],
]

function rampColor(per: number, out: number[], o: number) {
  const p = Math.min(1, Math.max(0, per)) * (RAMP.length - 1)
  const i = Math.min(RAMP.length - 2, Math.floor(p))
  const f = p - i
  for (let k = 0; k < 3; k++) out[o + k] = RAMP[i][k] + (RAMP[i + 1][k] - RAMP[i][k]) * f
}

const rampCss = (per: number) => {
  const c = [0, 0, 0]
  rampColor(per, c, 0)
  return `rgb(${c.map((v) => Math.round(v * 255)).join(',')})`
}

function values(c: AdbCase, q: Quantity): Float32Array | null {
  if (q === 'cp') return c.cp
  if (q === 'cpUnsteady') return c.cpUnsteady
  if (q === 'gamma') return c.gamma
  if (q === 'cpSteady') return c.cp.map((v, i) => v - c.cpUnsteady[i])
  return null
}

interface Stats {
  /** Actual range over the shown triangles (the legend's clip values) */
  lo: number
  hi: number
  /** The colour range: mean +/- one standard deviation within the actual range, as the
   * desktop viewer sets it (panel runs below Mach 1 fix Cp's lower end at -1.5). */
  auto: [number, number]
}

function stats(vals: Float32Array, visible: Uint32Array, panelSubsonicCp: boolean, cpMax: number): Stats {
  if (!visible.length) return { lo: 0, hi: 1, auto: [0, 1] }
  let lo = Infinity, hi = -Infinity, sum = 0, sum2 = 0
  for (const t of visible) {
    const v = vals[t]
    lo = Math.min(lo, v)
    hi = Math.max(hi, v)
    sum += v
    sum2 += v * v
  }
  const mean = sum / visible.length
  const sd = Math.sqrt(Math.max(0, sum2 / visible.length - mean * mean))
  if (panelSubsonicCp) return { lo, hi, auto: [-1.5, Math.min(hi, cpMax)] }
  const min = Math.max(lo, mean - sd)
  const max = Math.min(hi, mean + sd)
  return { lo, hi, auto: max > min ? [min, max] : [lo, hi > lo ? hi : lo + 1] }
}

interface Options {
  quantity: Quantity
  smooth: boolean
  wireframe: boolean
  wakes: boolean
  mirror: boolean
  range: [number, number]
}

function buildSurface(c: AdbCase, visible: Uint32Array, opts: Options) {
  const vals = values(c, opts.quantity)
  const [min, max] = opts.range
  const span = max - min || 1
  const geo = new THREE.BufferGeometry()
  if (opts.smooth && vals) {
    // Values averaged to the nodes, weighted by triangle area; smooth shading.
    const nNode = c.nodes.length / 3
    const acc = new Float64Array(nNode)
    const wt = new Float64Array(nNode)
    const idx = new Uint32Array(visible.length * 3)
    visible.forEach((t, i) => {
      for (let k = 0; k < 3; k++) {
        const n = c.tris[3 * t + k]
        idx[3 * i + k] = n
        acc[n] += vals[t] * c.area[t]
        wt[n] += c.area[t]
      }
    })
    const colors = new Array(nNode * 3).fill(0.85)
    for (let n = 0; n < nNode; n++) if (wt[n] > 0) rampColor((acc[n] / wt[n] - min) / span, colors, 3 * n)
    geo.setAttribute('position', new THREE.BufferAttribute(c.nodes, 3))
    geo.setAttribute('color', new THREE.Float32BufferAttribute(colors, 3))
    geo.setIndex(new THREE.BufferAttribute(idx, 1))
  } else {
    // One colour per triangle (the desktop viewer's default), flat shading.
    const pos = new Float32Array(visible.length * 9)
    const colors: number[] = new Array(visible.length * 9)
    const rgb = [0, 0, 0]
    visible.forEach((t, i) => {
      if (vals) rampColor((vals[t] - min) / span, rgb, 0)
      else rgb[0] = rgb[1] = rgb[2] = 0.85
      for (let k = 0; k < 3; k++) {
        const n = c.tris[3 * t + k]
        pos.set(c.nodes.subarray(3 * n, 3 * n + 3), 9 * i + 3 * k)
        colors[9 * i + 3 * k] = rgb[0]
        colors[9 * i + 3 * k + 1] = rgb[1]
        colors[9 * i + 3 * k + 2] = rgb[2]
      }
    })
    geo.setAttribute('position', new THREE.BufferAttribute(pos, 3))
    geo.setAttribute('color', new THREE.Float32BufferAttribute(colors, 3))
  }
  geo.computeVertexNormals()
  geo.computeBoundingBox()
  return geo
}

function buildWireframe(c: AdbCase, visible: Uint32Array) {
  const pts = new Float32Array(visible.length * 18)
  visible.forEach((t, i) => {
    const a = c.tris[3 * t], b = c.tris[3 * t + 1], d = c.tris[3 * t + 2]
    ;[a, b, b, d, d, a].forEach((n, k) => pts.set(c.nodes.subarray(3 * n, 3 * n + 3), 18 * i + 3 * k))
  })
  const geo = new THREE.BufferGeometry()
  geo.setAttribute('position', new THREE.BufferAttribute(pts, 3))
  return geo
}

function buildWakes(c: AdbCase, steady: boolean) {
  const segs: number[] = []
  for (const tr of c.trails) {
    // Steady wakes end at a far-field point; the desktop viewer leaves it out.
    const n = tr.points.length / 3 - (steady && tr.points.length / 3 > 2 ? 1 : 0)
    for (let j = 0; j + 1 < n; j++) segs.push(...tr.points.subarray(3 * j, 3 * j + 6))
  }
  const geo = new THREE.BufferGeometry()
  geo.setAttribute('position', new THREE.Float32BufferAttribute(segs, 3))
  return geo
}

interface SceneProps {
  adb: AdbFile
  c: AdbCase
  visible: Uint32Array
  opts: Options
  showCG: boolean
  showAxes: boolean
  ortho: boolean
  request: ViewRequest
  /** The model's own surfaces (not wakes): what fitting frames. */
  modelBox: THREE.Box3 | null
  /** Wakes or wake surfaces are shown: they reach well beyond the model. */
  deep: boolean
}

function Scene({ adb, c, visible, opts, showCG, showAxes, ortho, request, modelBox, deep }: SceneProps) {
  const surface = useMemo(() => buildSurface(c, visible, opts), [c, visible, opts])
  const wire = useMemo(() => (opts.wireframe ? buildWireframe(c, visible) : null), [c, visible, opts.wireframe])
  const wakes = useMemo(() => (opts.wakes ? buildWakes(c, !adb.header.timeAccurate) : null), [c, opts.wakes, adb])
  useEffect(() => () => surface.dispose(), [surface])
  useEffect(() => () => wire?.dispose(), [wire])
  useEffect(() => () => wakes?.dispose(), [wakes])

  const box = modelBox
  const getBounds = useCallback((): Promise<Box | null> => {
    if (!box) return Promise.resolve(null)
    const lo: [number, number, number] = [box.min.x, opts.mirror ? Math.min(box.min.y, -box.max.y) : box.min.y, box.min.z]
    const hi: [number, number, number] = [box.max.x, opts.mirror ? Math.max(box.max.y, -box.min.y) : box.max.y, box.max.z]
    return Promise.resolve({ min: lo, max: hi })
  }, [box, opts.mirror])
  const size = box ? box.getSize(new THREE.Vector3()).length() : 10
  const cg = adb.header.cg
  const copies = opts.mirror ? [1, -1] : [1]
  return (
    <>
      <color attach="background" args={['#161616']} />
      <hemisphereLight args={['#ffffff', '#3a3a3a', 0.9]} />
      <Navigation ortho={ortho} request={request} getBounds={getBounds} clipScale={deep ? 12 : 3} />
      {copies.map((s) => (
        <group key={s} scale={[1, s, 1]}>
          <mesh geometry={surface}>
            <meshLambertMaterial vertexColors side={THREE.DoubleSide} flatShading={!opts.smooth} />
          </mesh>
          {wire && (
            <lineSegments geometry={wire}>
              <lineBasicMaterial color="#000000" transparent opacity={0.35} />
            </lineSegments>
          )}
          {wakes && (
            <lineSegments geometry={wakes}>
              <lineBasicMaterial color="#5aa9ff" transparent opacity={0.8} />
            </lineSegments>
          )}
        </group>
      ))}
      {showCG && (
        <mesh position={cg}>
          <sphereGeometry args={[size * 0.008, 16, 12]} />
          <meshBasicMaterial color="#ffffff" />
        </mesh>
      )}
      {showAxes && <axesHelper args={[size * 0.25]} />}
    </>
  )
}

/** As the desktop viewer: title, the actual maximum (top clip box), 5 values, actual minimum. */
function Legend({ label, range, lo, hi }: { label: string; range: [number, number]; lo: number; hi: number }) {
  const [min, max] = range
  const ticks = [0, 0.25, 0.5, 0.75, 1].map((f) => max - f * (max - min))
  const grad = Array.from({ length: 11 }, (_, i) => `${rampCss(1 - i / 10)} ${i * 10}%`).join(', ')
  return (
    <div className="results-legend">
      <div className="results-legend-title">{label}</div>
      <div className="results-legend-clip">
        <span className="results-legend-swatch" style={{ background: rampCss(1) }} />
        {hi.toFixed(5)}
      </div>
      <div className="results-legend-body">
        <div className="results-legend-bar" style={{ background: `linear-gradient(to bottom, ${grad})` }} />
        <div className="results-legend-ticks">
          {ticks.map((v, i) => (
            <span key={i}>{v.toFixed(5)}</span>
          ))}
        </div>
      </div>
      <div className="results-legend-clip">
        <span className="results-legend-swatch" style={{ background: rampCss(0) }} />
        {lo.toFixed(5)}
      </div>
    </div>
  )
}

// The case's coefficients worth showing, from <model>.polar.
const COEFFS: [string, string][] = [
  ['CLtot', 'CL'],
  ['CDo', 'CDo'],
  ['CDi', 'CDi'],
  ['CDtot', 'CD'],
  ['CStot', 'CS'],
  ['L/D', 'L/D'],
  ['E', 'E'],
  ['CMxtot', 'CMx'],
  ['CMytot', 'CMy'],
  ['CMztot', 'CMz'],
]

/** A comment from .adb.cases worth showing (not just the case number). */
const usefulComment = (s?: string) => !!s && !/^(no comment|case:?\s*\d+\b.*)$/i.test(s.trim())

export function ResultsViewer() {
  const { open, name, adb, cases, polar, error, close } = useViewer()
  const [caseIdx, setCaseIdx] = useState(0)
  const [quantity, setQuantity] = useState<Quantity>('cp')
  const [smooth, setSmooth] = useState(false)
  const [wireframe, setWireframe] = useState(false)
  const [wakes, setWakes] = useState(true)
  const [wakeSurfaces, setWakeSurfaces] = useState(false)
  const [mirror, setMirror] = useState(false)
  const [showCG, setShowCG] = useState(false)
  const [showAxes, setShowAxes] = useState(false)
  const [manual, setManual] = useState<[number, number] | null>(null)
  // Navigation, as in the main view (its own camera; starts in the main view's projection).
  const [ortho, setOrtho] = useState(() => useStore.getState().ortho)
  const [request, setRequest] = useState<ViewRequest>({ name: 'leftiso', n: 0 })
  const requestView = useCallback((v: ViewName) => setRequest((r) => ({ name: v, n: r.n + 1 })), [])
  const toggleOrtho = useCallback(() => setOrtho((o) => !o), [])
  const keysActive = useCallback(() => useViewer.getState().open && !document.querySelector('.modal-backdrop'), [])
  useViewKeys(requestView, toggleOrtho, keysActive)

  useEffect(() => {
    setCaseIdx(0)
    setManual(null)
    setMirror(!!adb?.header.symmetry)
    setRequest((r) => ({ name: 'leftiso', n: r.n + 1 }))
  }, [adb])

  const count = useMemo(() => {
    try {
      return adb ? adb.caseCount : 0
    } catch {
      return 0
    }
  }, [adb])
  const [c, readError] = useMemo((): [AdbCase | null, string | null] => {
    if (!adb || !count) return [null, null]
    try {
      return [adb.getCase(Math.min(caseIdx, count - 1)), null]
    } catch (err) {
      return [null, err instanceof Error ? err.message : String(err)]
    }
  }, [adb, caseIdx, count])

  // Shown triangles: the surfaces, plus wake surfaces if asked (unsteady: from their first step).
  const visible = useMemo(() => {
    const out: number[] = []
    if (c) for (let t = 0; t < c.surfaceId.length; t++) {
      if (c.surfaceId[t] > 0 || (wakeSurfaces && c.minValidStep[t] <= caseIdx)) out.push(t)
    }
    return Uint32Array.from(out)
  }, [c, wakeSurfaces, caseIdx])

  const modelBox = useMemo(() => {
    if (!c) return null
    const b = new THREE.Box3()
    const p = new THREE.Vector3()
    for (let t = 0; t < c.surfaceId.length; t++) {
      if (c.surfaceId[t] <= 0) continue
      for (let k = 0; k < 3; k++) {
        const n = c.tris[3 * t + k]
        b.expandByPoint(p.set(c.nodes[3 * n], c.nodes[3 * n + 1], c.nodes[3 * n + 2]))
      }
    }
    return b.isEmpty() ? null : b
  }, [c])

  const vlm = adb?.header.modelType === 1
  const st = useMemo((): Stats => {
    if (!c || !adb) return { lo: 0, hi: 1, auto: [0, 1] }
    const vals = values(c, quantity)
    if (!vals) return { lo: 0, hi: 1, auto: [0, 1] }
    const panelCp = adb.header.modelType === 2 && c.mach < 1 && (quantity === 'cp' || quantity === 'cpSteady')
    return stats(vals, visible, panelCp, c.cpMax)
  }, [c, adb, quantity, visible])
  const range = manual ?? st.auto
  const opts = useMemo<Options>(
    () => ({ quantity, smooth, wireframe, wakes, mirror, range }),
    [quantity, smooth, wireframe, wakes, mirror, range],
  )
  const coeffs = useMemo(() => (c && polar ? polarRow(polar, c.mach, c.alpha, c.beta) : null), [c, polar])

  if (!open) return null
  const info = cases[caseIdx]
  const label = QUANTITIES.find((q) => q.key === quantity)!.label(vlm)
  const h = adb?.header
  return (
    <ViewerWindow title={`VSPAERO Viewer · ${name}`} onClose={close}>
      <div className="results-bar">
        <select value={quantity} onChange={(e) => setQuantity(e.target.value as Quantity)} title="What the surface colours show">
          {QUANTITIES.map((q) => (
            <option key={q.key} value={q.key}>
              {q.label(vlm)}
            </option>
          ))}
        </select>
        <label><input type="checkbox" checked={smooth} onChange={(e) => setSmooth(e.target.checked)} /> Smooth</label>
        <label><input type="checkbox" checked={wireframe} onChange={(e) => setWireframe(e.target.checked)} /> Mesh</label>
        <label><input type="checkbox" checked={wakes} onChange={(e) => setWakes(e.target.checked)} /> Wakes</label>
        <label><input type="checkbox" checked={wakeSurfaces} onChange={(e) => setWakeSurfaces(e.target.checked)} /> Wake surfaces</label>
        <label><input type="checkbox" checked={mirror} onChange={(e) => setMirror(e.target.checked)} /> Reflect</label>
        <label><input type="checkbox" checked={showCG} onChange={(e) => setShowCG(e.target.checked)} /> CG</label>
        <label><input type="checkbox" checked={showAxes} onChange={(e) => setShowAxes(e.target.checked)} /> Axes</label>
        {quantity !== 'shaded' && (
          <span className="results-range">
            <label><input type="checkbox" checked={!manual} onChange={(e) => setManual(e.target.checked ? null : st.auto)} /> Auto range</label>
            {manual && (
              <>
                <input type="number" step="any" value={manual[0]} onChange={(e) => setManual([Number(e.target.value), manual[1]])} title="Minimum" />
                <input type="number" step="any" value={manual[1]} onChange={(e) => setManual([manual[0], Number(e.target.value)])} title="Maximum" />
              </>
            )}
          </span>
        )}
      </div>
      <div className="results-body">
        {error || readError ? (
          <div className="results-error">Can't show these results: {error || readError}</div>
        ) : c && adb && h ? (
          <>
            <Canvas dpr={[1, 2]} resize={{ scroll: false, debounce: 0 }}>
              <Scene adb={adb} c={c} visible={visible} opts={opts} showCG={showCG} showAxes={showAxes} ortho={ortho} request={request} modelBox={modelBox} deep={wakes || wakeSurfaces} />
            </Canvas>
            <ViewTools onView={requestView} ortho={ortho} onToggleOrtho={toggleOrtho} />
            {quantity !== 'shaded' && <Legend label={label} range={range} lo={st.lo} hi={st.hi} />}
            <div className="results-panel">
              <div className="results-case">
                <span>Solution Case</span>
                <button className="ghost" disabled={caseIdx <= 0} onClick={() => setCaseIdx(caseIdx - 1)} title="Previous case">
                  ◀
                </button>
                <select value={caseIdx} onChange={(e) => setCaseIdx(Number(e.target.value))}>
                  {Array.from({ length: count }, (_, i) => (
                    <option key={i} value={i}>
                      {h.timeAccurate ? i : i + 1}
                    </option>
                  ))}
                </select>
                <button className="ghost" disabled={caseIdx >= count - 1} onClick={() => setCaseIdx(caseIdx + 1)} title="Next case">
                  ▶
                </button>
                <span>of {h.timeAccurate ? count - 1 : count}</span>
              </div>
              <dl>
                <dt>Mach</dt><dd>{c.mach.toFixed(4)}</dd>
                <dt>Alpha</dt><dd>{c.alpha.toFixed(4)}°</dd>
                <dt>Beta</dt><dd>{c.beta.toFixed(4)}°</dd>
              </dl>
              {usefulComment(info?.comment) && <div className="results-comment">{info.comment}</div>}
              {coeffs && (
                <>
                  <div className="results-section">Coefficients</div>
                  <dl>
                    {COEFFS.filter(([k]) => k in coeffs).map(([k, label]) => (
                      <div key={k} className="results-row">
                        <dt>{label}</dt><dd>{coeffs[k].toFixed(5)}</dd>
                      </div>
                    ))}
                  </dl>
                </>
              )}
              <div className="results-section">Reference</div>
              <dl>
                <dt>Sref</dt><dd>{h.sref.toPrecision(6)}</dd>
                <dt>Cref</dt><dd>{h.cref.toPrecision(6)}</dd>
                <dt>Bref</dt><dd>{h.bref.toPrecision(6)}</dd>
                {showCG && (
                  <>
                    <dt>CG</dt><dd>{h.cg.map((v) => v.toFixed(4)).join(', ')}</dd>
                  </>
                )}
                <dt>Model</dt><dd>{vlm ? 'Vortex lattice' : 'Panel'}{h.symmetry ? ', symmetric' : ''}</dd>
              </dl>
            </div>
            <div className="results-label">
              Mach: {c.mach.toFixed(3)}, Beta: {c.beta.toFixed(3)}, Alpha: {c.alpha.toFixed(3)}
            </div>
          </>
        ) : (
          <div className="results-error">No solution cases in this results file.</div>
        )}
      </div>
    </ViewerWindow>
  )
}

type Rect = { x: number; y: number; w: number; h: number }
type Edge = 'n' | 's' | 'e' | 'w' | 'ne' | 'nw' | 'se' | 'sw'
const EDGES: Edge[] = ['n', 's', 'e', 'w', 'ne', 'nw', 'se', 'sw']
const MIN_W = 420
const MIN_H = 280

/**
 * A window like the other floating windows: moved by its title bar, resized from its edges
 * and corners, brought to the front on click. Opens at 90% of the main area, centered;
 * double-click the title or use the button to maximize/restore.
 */
function ViewerWindow({ title, onClose, children }: { title: string; onClose: () => void; children: ReactNode }) {
  const ref = useRef<HTMLDivElement>(null)
  const [maximized, setMaximized] = useState(false)
  const [rect, setRect] = useState<Rect | null>(null)
  const [z, setZ] = useState(() => nextZ())
  const drag = useRef<{ edge: Edge | 'move'; x: number; y: number; start: Rect } | null>(null)

  /** The area the window lives in (the main area, its offset parent). */
  const area = () => {
    const p = ref.current?.offsetParent as HTMLElement | null
    return { w: p?.clientWidth ?? window.innerWidth, h: p?.clientHeight ?? window.innerHeight }
  }
  /** The restored size: 90% of the area, centered. */
  const defaultRect = (): Rect => {
    const a = area()
    const w = Math.max(MIN_W, Math.round(a.w * 0.9)), h = Math.max(MIN_H, Math.round(a.h * 0.9))
    return { x: Math.round((a.w - w) / 2), y: Math.round((a.h - h) / 2), w, h }
  }

  const begin = (edge: Edge | 'move') => (e: React.PointerEvent) => {
    if (e.button !== 0 || (e.target as HTMLElement).closest('button')) return
    e.preventDefault()
    let start = rect ?? defaultRect()
    if (maximized) {
      if (edge !== 'move') return
      // Dragging a maximized window restores it under the pointer.
      const a = area()
      const box = ref.current!.getBoundingClientRect()
      const fx = (e.clientX - box.left) / box.width
      start = { ...start, x: Math.round(e.clientX - box.left - fx * start.w), y: 0 }
      start.x = Math.min(Math.max(0, start.x), Math.max(0, a.w - start.w))
      setMaximized(false)
      setRect(start)
    }
    ;(e.currentTarget as HTMLElement).setPointerCapture(e.pointerId)
    drag.current = { edge, x: e.clientX, y: e.clientY, start }
  }
  const move = (e: React.PointerEvent) => {
    const d = drag.current
    if (!d) return
    const dx = e.clientX - d.x, dy = e.clientY - d.y
    const a = area()
    let { x, y, w, h } = d.start
    if (d.edge === 'move') {
      x = Math.min(Math.max(-w + 80, x + dx), a.w - 80)
      y = Math.min(Math.max(0, y + dy), a.h - 40)
    } else {
      if (d.edge.includes('e')) w = Math.max(MIN_W, w + dx)
      if (d.edge.includes('s')) h = Math.max(MIN_H, h + dy)
      if (d.edge.includes('w')) { const nw = Math.max(MIN_W, w - dx); x += w - nw; w = nw }
      if (d.edge.includes('n')) { const nh = Math.max(MIN_H, h - dy); y = Math.max(0, y + h - nh); h = nh }
    }
    setRect({ x, y, w, h })
  }
  const end = () => (drag.current = null)

  // Until first moved or resized, the 90% box is relative, so it follows the area's size.
  const style: React.CSSProperties = maximized
    ? { inset: 0, zIndex: z }
    : rect
      ? { left: rect.x, top: rect.y, width: rect.w, height: rect.h, zIndex: z }
      : { left: '5%', top: '5%', width: '90%', height: '90%', zIndex: z }
  return (
    <div
      ref={ref}
      className={'results-viewer' + (maximized ? ' maximized' : '')}
      style={style}
      role="dialog"
      aria-label={title}
      onPointerDownCapture={() => !isTopZ(z) && setZ(nextZ())}
    >
      <div
        className="m-float-title results-titlebar"
        onPointerDown={begin('move')}
        onPointerMove={move}
        onPointerUp={end}
        onDoubleClick={(e) => !(e.target as HTMLElement).closest('button') && setMaximized((m) => !m)}
      >
        <span>{title}</span>
        <span className="results-titlebuttons">
          <button className="ghost" title={maximized ? 'Restore' : 'Maximize'} onClick={() => setMaximized((m) => !m)}>
            {maximized ? '❐' : '▢'}
          </button>
          <button className="ghost" title="Close the viewer" onClick={onClose}>
            ✕
          </button>
        </span>
      </div>
      {children}
      {!maximized &&
        EDGES.map((edge) => (
          <div key={edge} className={`results-resize ${edge}`} onPointerDown={begin(edge)} onPointerMove={move} onPointerUp={end} />
        ))}
    </div>
  )
}
