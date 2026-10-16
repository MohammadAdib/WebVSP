import { Html, Line, TransformControls } from '@react-three/drei'
import { Canvas, useFrame, useThree, type ThreeEvent } from '@react-three/fiber'
import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import * as THREE from 'three'
import type { TransformControls as TransformControlsImpl } from 'three-stdlib'
import { create } from 'zustand'
import { api } from '../api/client'
import { decodeMesh, gridLineIndices } from '../api/mesh'
import { NO_GEOMS, useStore } from '../store'
import { useViewer } from '../viewer/store'
import { useMirror } from '../mirror/store'
import type { GlDrawObj } from '../mirror/types'
import { Navigation, useViewKeys, ViewTools } from './camera'

/** Opacity of the selected geom's bounding box (red) and of the active wing section's box (green). */
export const BBOX_ALPHA = 0.5
export const SECTION_BOX_ALPHA = 0.5
/** The move gizmo snaps a geom's origin to this grid (model units). */
const MOVE_SNAP = 1
/** Radius of the move gizmo's axis lines, in gizmo units (its cones are 0.05 at the base). */
const GIZMO_AXIS_RADIUS = 0.008

/** Shift- or Ctrl-click (Cmd on a Mac) adds a geom to the selection or takes it out. */
const isToggleClick = (e: { shiftKey: boolean; ctrlKey: boolean; metaKey: boolean }) => e.shiftKey || e.ctrlKey || e.metaKey

/** When the move gizmo was last pressed: a click on it isn't a click on nothing. */
let gizmoPressedAt = 0

const ZERO: [number, number, number] = [0, 0, 0]
const NO_IDS: string[] = []

/** The move gizmo's drag so far: the geoms, how far, and their meshRevs when it started. */
const useMovePreview = create<{ ids: readonly string[]; d: [number, number, number]; revs: Record<string, number> }>(
  () => ({ ids: [], d: ZERO, revs: {} }),
)

const SHADE = new THREE.Color('#a6a6a6')
const SHADE_SEL = new THREE.Color('#dedede')
const LINE_ON_SHADE = new THREE.Color('#202020')
const LINE_WIRE = new THREE.Color('#8f8f8f')
const LINE_SEL = new THREE.Color('#f2f2f2')
/** The outline (OpenVSP's feature lines): black and 1 px, like the desktop's. */
export const OUTLINE_COLOR = '#000000'
export const OUTLINE_WIDTH = 1

/** Per-frame view state written by CameraRig, read by meshes (avoids React re-renders). */
const view = {
  /** 1 when close, 0 when zoomed far out: dense wireframe on a tiny model reads as noise. */
  wireFade: 1,
}

interface Built {
  /** The meshRev it was built for */
  rev?: number
  surfaces: {
    mesh: THREE.BufferGeometry
    lines: THREE.BufferGeometry
    /** The desktop's colour for this surface (a Mesh / NGon Mesh tag) */
    color?: THREE.Color
  }[]
  /** The outline (feature lines) as segment end points, x,y,z flat */
  outline: number[]
}

function buildGeometry(buf: ArrayBuffer): Built {
  const decoded = decodeMesh(buf)
  return {
    outline: decoded.features ? Array.from(decoded.features) : [],
    surfaces: decoded.map((s) => {
      const pos = new THREE.BufferAttribute(s.positions, 3)
      const mesh = new THREE.BufferGeometry()
      mesh.setAttribute('position', pos)
      mesh.setAttribute('normal', new THREE.BufferAttribute(s.normals, 3))
      mesh.setIndex(new THREE.BufferAttribute(s.indices, 1))
      mesh.computeBoundingSphere()
      const lines = new THREE.BufferGeometry()
      if (s.edges) {
        // NGon faces: their polygon edges, not the triangles'.
        lines.setAttribute('position', new THREE.BufferAttribute(s.edges, 3))
      } else {
        lines.setAttribute('position', pos)
        lines.setIndex(new THREE.BufferAttribute(gridLineIndices(s.nu, s.nw), 1))
      }
      return { mesh, lines, color: s.color ? new THREE.Color(...s.color) : undefined }
    }),
  }
}

function GeomMesh({ id }: { id: string }) {
  const rev = useStore((s) => s.meshRev[id] ?? 0)
  const selected = useStore((s) => s.tree?.active?.includes(id) ?? false)
  // Each geom's own draw type, as set in the Geom Browser (Wire / Hidden / Shade / Texture / None).
  const drawType = useStore((s) => s.geomsById[id]?.drawType ?? 2)
  const mode: 'wire' | 'hidden' | 'shaded' | 'none' =
    drawType === 0 ? 'wire' : drawType === 1 ? 'hidden' : drawType === 4 ? 'none' : 'shaded'
  const [built, setBuilt] = useState<Built | null>(null)
  // Dragged by the move gizmo: drawn moved until the mesh of the moved geom arrives.
  const offset = useMovePreview((s) => (s.ids.includes(id) && (built?.rev ?? 0) <= (s.revs[id] ?? -1) ? s.d : null))

  useEffect(() => {
    let cancelled = false
    api
      .mesh(id)
      .then((buf) => {
        if (!cancelled) setBuilt({ ...buildGeometry(buf), rev })
      })
      .catch(() => {})
    return () => {
      cancelled = true
    }
  }, [id, rev])

  useEffect(
    () => () => {
      built?.surfaces.forEach((s) => {
        s.mesh.dispose()
        s.lines.dispose()
      })
    },
    [built],
  )

  const shadeMat = useMemo(
    () =>
      new THREE.MeshStandardMaterial({
        side: THREE.DoubleSide,
        flatShading: true, // faceted, one normal per triangle
        roughness: 0.65,
        metalness: 0.05,
        polygonOffset: true,
        polygonOffsetFactor: 1,
        polygonOffsetUnits: 1,
      }),
    [],
  )
  const lineMat = useMemo(() => new THREE.LineBasicMaterial({ transparent: true }), [])
  useEffect(() => () => {
    shadeMat.dispose()
    lineMat.dispose()
  }, [shadeMat, lineMat])
  // Surfaces the desktop colours (Mesh / NGon Mesh tags) get materials of their own.
  const tagMats = useMemo(
    () =>
      built?.surfaces.map((s) =>
        s.color ? { color: s.color, shade: shadeMat.clone(), line: lineMat.clone() } : null,
      ) ?? [],
    [built, shadeMat, lineMat],
  )
  useEffect(() => () => tagMats.forEach((m) => {
    m?.shade.dispose()
    m?.line.dispose()
  }), [tagMats])

  if (mode === 'hidden') {
    // Hidden-line: surfaces in the background color occlude the lines behind them.
    shadeMat.color.set('#161616')
    shadeMat.emissive.set('#161616')
  } else {
    shadeMat.color.copy(selected ? SHADE_SEL : SHADE)
    shadeMat.emissive.set(selected ? '#262626' : '#000000')
  }
  lineMat.color.copy(selected ? LINE_SEL : mode === 'shaded' ? LINE_ON_SHADE : LINE_WIRE)
  for (const m of tagMats) {
    if (!m) continue
    // Like the desktop's tag colours: shaded in a tint of the colour, edges in the colour.
    m.shade.color.copy(shadeMat.color)
    m.shade.emissive.copy(shadeMat.emissive)
    if (mode !== 'hidden') m.shade.color.lerp(m.color, selected ? 0.5 : 0.6)
    m.line.color.copy(mode === 'shaded' ? lineMat.color : m.color)
    if (selected && mode !== 'shaded') m.line.color.lerp(LINE_SEL, 0.35)
  }
  useFrame(() => {
    // Pure wire mode keeps its lines; over shading they fade out as the model gets small.
    // Shaded geoms show their wireframe only when selected, as in the FLTK view.
    const base = mode === 'shaded' ? (selected ? 0.9 : 0) : 1
    const fade = mode === 'shaded' ? view.wireFade : 1
    lineMat.opacity = base * fade
    lineMat.visible = lineMat.opacity > 0.02
    for (const m of tagMats) {
      if (!m) continue
      m.line.opacity = lineMat.opacity
      m.line.visible = lineMat.visible
    }
  })

  const onClick = (e: ThreeEvent<MouseEvent>) => {
    if (e.delta > 4) return // was an orbit drag
    e.stopPropagation()
    void api.pick(id, isToggleClick(e.nativeEvent))
  }

  if (!built || mode === 'none') return null
  return (
    <group onClick={onClick} position={offset ?? ZERO}>
      {built.surfaces.map((s, i) => (
        <group key={i}>
          {(mode === 'shaded' || mode === 'hidden') && <mesh geometry={s.mesh} material={tagMats[i]?.shade ?? shadeMat} />}
          <lineSegments geometry={s.lines} material={tagMats[i]?.line ?? lineMat} />
          {mode === 'wire' && (
            // invisible surface so wireframe geoms remain clickable
            <mesh geometry={s.mesh} visible={false} />
          )}
        </group>
      ))}
      {built.outline.length >= 6 && (
        <Line points={built.outline} segments color={OUTLINE_COLOR} lineWidth={OUTLINE_WIDTH} />
      )}
    </group>
  )
}

/** The ground grid's colours: its lines (fine, and every 10th brighter), and the X (red) and
 *  Y (green) axes. */
const GRID_COLOR = '#424242'
const GRID_MAJOR_COLOR = '#575757'
const GRID_X_AXIS = '#b04646'
const GRID_Y_AXIS = '#5f9a3e'
/** The finest grid spacing drawn, in pixels on screen; finer lines fade out (Blender-style). */
const GRID_MIN_PIXELS = 14
/** Where the grid has faded out, as a multiple of the model's footprint (fully drawn up to 1). */
const GRID_FADE_END = 1.5
/** At most this many lines each way per level: zoomed far in, only a window of them around the
 *  point the view orbits is drawn, following it. */
const GRID_MAX_LINES = 1500

interface GridLines {
  pos: number[]
  col: number[]
}

/**
 * Adds a line to `out` in pieces, each end with alpha fading out from the model's footprint
 * (`half` around cx, cy) to GRID_FADE_END times it, so the fade along it is smooth.
 */
function fadedLine(out: GridLines, cx: number, cy: number, half: number, x0: number, y0: number, x1: number, y1: number, c: THREE.Color) {
  const alpha = (x: number, y: number) => {
    const u = THREE.MathUtils.clamp((Math.max(Math.abs(x - cx), Math.abs(y - cy)) / half - 1) / (GRID_FADE_END - 1), 0, 1)
    return 1 - u * u * (3 - 2 * u)
  }
  const pieces = 32
  for (let k = 0; k < pieces; k++) {
    const a = k / pieces
    const b = (k + 1) / pieces
    const ax = x0 + (x1 - x0) * a, ay = y0 + (y1 - y0) * a
    const bx = x0 + (x1 - x0) * b, by = y0 + (y1 - y0) * b
    const fa = alpha(ax, ay), fb = alpha(bx, by)
    if (fa <= 0 && fb <= 0) continue
    out.pos.push(ax, ay, 0, bx, by, 0)
    out.col.push(c.r, c.g, c.b, fa, c.r, c.g, c.b, fb)
  }
}

/**
 * One level of the grid: lines every `step`, aligned to the origin, over the square
 * GRID_FADE_END times the footprint around the center, limited to GRID_MAX_LINES lines each way around (wx, wy). The
 * axes are left out: they're drawn on their own.
 */
function gridLevel(cx: number, cy: number, half: number, step: number, wx: number, wy: number, color: THREE.Color): GridLines {
  const reach = (GRID_MAX_LINES / 2) * step
  const ext = GRID_FADE_END * half
  const x0 = Math.max(cx - ext, wx - reach), x1 = Math.min(cx + ext, wx + reach)
  const y0 = Math.max(cy - ext, wy - reach), y1 = Math.min(cy + ext, wy + reach)
  const out: GridLines = { pos: [], col: [] }
  if (x0 >= x1 || y0 >= y1) return out
  for (let i = Math.ceil(x0 / step); i * step <= x1; i++) if (i !== 0) fadedLine(out, cx, cy, half, i * step, y0, i * step, y1, color)
  for (let j = Math.ceil(y0 / step); j * step <= y1; j++) if (j !== 0) fadedLine(out, cx, cy, half, x0, j * step, x1, j * step, color)
  return out
}

function toGeometry(parts: GridLines[]) {
  const geo = new THREE.BufferGeometry()
  geo.setAttribute('position', new THREE.Float32BufferAttribute(parts.flatMap((p) => p.pos), 3))
  geo.setAttribute('color', new THREE.Float32BufferAttribute(parts.flatMap((p) => p.col), 4))
  return geo
}

/**
 * The ground grid on the Z = 0 plane: a square around the model as wide as its longest side in
 * plan, fully drawn over it and fading out to GRID_FADE_END times its size. One spacing for the whole grid,
 * from the zoom: the finest round spacing (..., 0.1, 1, 10, ...) at least GRID_MIN_PIXELS apart
 * on screen, fading out as it nears that, with the next one (10x) over it; so zooming brings
 * finer lines in and out, like Blender's grid. The X and Y axes are drawn in colour.
 */
function GroundGrid({ center, size }: { center: [number, number]; size: number }) {
  const half = Math.max(size, 1e-6) / 2
  const [cx, cy] = center
  // The zoom level, and where the view orbits (the window drawn when zoomed far in).
  const [view0, setView0] = useState<{ level: number; wx: number; wy: number } | null>(null)
  const fineMat = useRef<THREE.LineBasicMaterial>(null)
  const { camera, controls, size: screen } = useThree()

  // The zoom: model units per pixel at the point the view orbits around.
  useFrame(() => {
    const target = (controls as unknown as { target?: THREE.Vector3 } | null)?.target ?? new THREE.Vector3(cx, cy, 0)
    let perPixel: number
    if ((camera as THREE.OrthographicCamera).isOrthographicCamera) {
      const o = camera as THREE.OrthographicCamera
      perPixel = (o.top - o.bottom) / o.zoom / Math.max(screen.height, 1)
    } else {
      const p = camera as THREE.PerspectiveCamera
      perPixel = (2 * camera.position.distanceTo(target) * Math.tan(THREE.MathUtils.degToRad(p.fov / 2))) / Math.max(screen.height, 1)
    }
    const lod = Math.log10(Math.max(perPixel, 1e-12) * GRID_MIN_PIXELS)
    const next = Math.floor(lod)
    // Follow the view when it has moved a good part of the way across the drawn window.
    const far = view0 && Math.max(Math.abs(target.x - view0.wx), Math.abs(target.y - view0.wy)) > (GRID_MAX_LINES / 8) * 10 ** (next + 1)
    if (!view0 || next !== view0.level || far) setView0({ level: next, wx: target.x, wy: target.y })
    // The finer level fades out as its lines close in on GRID_MIN_PIXELS apart.
    if (fineMat.current) fineMat.current.opacity = 1 - (lod - next)
  })

  const geo = useMemo(() => {
    if (!view0) return null
    const { level, wx, wy } = view0
    const fine = gridLevel(cx, cy, half, 10 ** (level + 1), wx, wy, new THREE.Color(GRID_COLOR))
    const coarse = gridLevel(cx, cy, half, 10 ** (level + 2), wx, wy, new THREE.Color(GRID_MAJOR_COLOR))
    const axes: GridLines = { pos: [], col: [] }
    const ext = GRID_FADE_END * half
    if (Math.abs(cy) <= ext) fadedLine(axes, cx, cy, half, cx - ext, 0, cx + ext, 0, new THREE.Color(GRID_X_AXIS))
    if (Math.abs(cx) <= ext) fadedLine(axes, cx, cy, half, 0, cy - ext, 0, cy + ext, new THREE.Color(GRID_Y_AXIS))
    return { fine: toGeometry([fine]), coarse: toGeometry([coarse, axes]) }
  }, [view0, cx, cy, half])
  useEffect(() => () => {
    geo?.fine.dispose()
    geo?.coarse.dispose()
  }, [geo])

  if (!geo) return null
  return (
    <group renderOrder={-1}>
      <lineSegments geometry={geo.fine} raycast={() => {}}>
        <lineBasicMaterial ref={fineMat} vertexColors transparent depthWrite={false} />
      </lineSegments>
      <lineSegments geometry={geo.coarse} raycast={() => {}}>
        <lineBasicMaterial vertexColors transparent depthWrite={false} />
      </lineSegments>
    </group>
  )
}

function Scene() {
  const geoms = useStore((s) => s.tree?.geoms ?? NO_GEOMS)
  const shown = geoms.filter((g) => g.shown)
  const bounds = useStore((s) => s.bounds)
  const ortho = useStore((s) => s.ortho)
  const request = useStore((s) => s.viewRequest)
  const setBounds = useStore((s) => s.setBounds)
  const showGrid = useStore((s) => s.grid)
  // The grid follows the model: its bounds are read again whenever the model changes (not
  // only when the view is fitted), and the grid rebuilt when they moved.
  const valuesRev = useStore((s) => s.valuesRev)
  const [gridKey, setGridKey] = useState('')
  useEffect(() => {
    let stale = false
    const t = window.setTimeout(() => {
      api.bbox().then(
        (b) => !stale && setGridKey(b ? JSON.stringify(b) : ''),
        () => undefined,
      )
    }, 150)
    return () => {
      stale = true
      window.clearTimeout(t)
    }
  }, [valuesRev])
  // Where the grid goes: under the model's footprint, square on its longest side in plan.
  const gridPlace = useMemo(() => {
    const b = gridKey
      ? (JSON.parse(gridKey) as { min: [number, number, number]; max: [number, number, number] })
      : bounds
        ? {
            min: bounds.center.map((c, k) => c - bounds.size[k] / 2) as [number, number, number],
            max: bounds.center.map((c, k) => c + bounds.size[k] / 2) as [number, number, number],
          }
        : { min: [-10, -10, 0] as [number, number, number], max: [10, 10, 0] as [number, number, number] }
    const center: [number, number] = [(b.min[0] + b.max[0]) / 2, (b.min[1] + b.max[1]) / 2]
    return { center, size: Math.max(b.max[0] - b.min[0], b.max[1] - b.min[1]) }
  }, [gridKey, bounds])

  return (
    <>
      <color attach="background" args={['#161616']} />
      <hemisphereLight args={['#ffffff', '#3a3a3a', 0.9]} />
      <Navigation
        ortho={ortho}
        request={request}
        getBounds={api.bbox}
        onBounds={setBounds}
        // Fade dense wireframe as the model gets small on screen.
        onFrame={(radii) => (view.wireFade = THREE.MathUtils.clamp((14 - radii) / 8, 0, 1))}
      />
      {showGrid && <GroundGrid center={gridPlace.center} size={gridPlace.size} />}
      {shown.map((g) => (
        <GeomMesh key={g.id} id={g.id} />
      ))}
      <Overlay />
      <MoveGizmo />
    </>
  )
}

export function Viewport() {
  const ortho = useStore((s) => s.ortho)
  const requestView = useStore((s) => s.requestView)
  const toggleOrtho = useStore((s) => s.toggleOrtho)
  const grid = useStore((s) => s.grid)
  const toggleGrid = useStore((s) => s.toggleGrid)
  // The keys are the results viewer's while it's open (it covers this view).
  const active = useCallback(
    () => !document.querySelector('.modal-backdrop') && !useViewer.getState().open,
    [],
  )
  useViewKeys(requestView, toggleOrtho, active)
  const down = useRef<[number, number] | null>(null)
  return (
    <div className="viewport">
      <ViewTools onView={requestView} ortho={ortho} onToggleOrtho={toggleOrtho} grid={grid} onToggleGrid={toggleGrid} />
      {/* Resize on every frame while the side panels slide: the default (debounced) resize
          stretches the old frame for the whole animation and then snaps. */}
      <Canvas
        dpr={[1, 2]}
        resize={{ scroll: false, debounce: 0 }}
        onPointerDown={(e) => (down.current = [e.clientX, e.clientY])}
        onPointerMissed={(e) => {
          // A click on nothing deselects everything; an orbit or pan drag doesn't.
          const d = down.current
          if (performance.now() - gizmoPressedAt < 1000) return
          if (e.button === 0 && !isToggleClick(e) && d && Math.hypot(e.clientX - d[0], e.clientY - d[1]) <= 4) void api.pick('')
        }}
      >
        <Scene />
      </Canvas>
    </div>
  )
}

type GizmoHandle = THREE.Object3D & { tag?: string }
interface Gizmo extends THREE.Object3D {
  showX: boolean
  showY: boolean
  showZ: boolean
  gizmo: Record<string, THREE.Object3D>
  picker: Record<string, THREE.Object3D>
  outward?: boolean
}

/**
 * Keep the move arrows (lines and cones) pointing the positive way along their axes wherever
 * the camera is. three.js mirrors them to the camera's side; undo that after each update.
 */
function keepArrowsOutward(controls: TransformControlsImpl) {
  const gz = (controls as unknown as { gizmo: Gizmo }).gizmo
  if (!gz || gz.outward) return
  gz.outward = true
  // WebGL draws lines 1 px wide: thicken each axis line with a thin cylinder along it, drawn
  // like the line (same colour and highlighting, always on top).
  const thick: [THREE.Line, THREE.Mesh<THREE.BufferGeometry, THREE.MeshBasicMaterial>][] = []
  for (const h of gz.gizmo.translate.children) {
    if (!(h instanceof THREE.Line) || !['X', 'Y', 'Z'].includes(h.name)) continue
    const geo = new THREE.CylinderGeometry(GIZMO_AXIS_RADIUS, GIZMO_AXIS_RADIUS, 1, 12, 1, true)
    geo.rotateZ(-Math.PI / 2) // along +X, like the line's geometry (0,0,0)-(1,0,0)
    geo.translate(0.5, 0, 0)
    const mat = new THREE.MeshBasicMaterial({ depthTest: false, depthWrite: false, transparent: true, fog: false, toneMapped: false })
    const tube = new THREE.Mesh(geo, mat)
    tube.raycast = () => {} // picking stays with the gizmo's own pick areas
    h.add(tube)
    thick.push([h, tube])
  }
  const update = gz.updateMatrixWorld.bind(gz)
  gz.updateMatrixWorld = (force?: boolean) => {
    update(force)
    for (const [line, tube] of thick) {
      const m = line.material as THREE.LineBasicMaterial
      tube.material.color.copy(m.color)
      tube.material.opacity = m.opacity
    }
    for (const group of [gz.gizmo.translate, gz.picker.translate]) {
      for (const h of group.children as GizmoHandle[]) {
        h.scale.set(Math.abs(h.scale.x), Math.abs(h.scale.y), Math.abs(h.scale.z))
        // An axis pointing at the camera is shrunk to nothing; leave it hidden.
        const shrunk = h.scale.x < 1e-8
        const a = h.name
        const shown = (!a.includes('X') || gz.showX) && (!a.includes('Y') || gz.showY) && (!a.includes('Z') || gz.showZ)
        if (h.tag === 'bwd') h.visible = false
        else if (h.tag === 'fwd') h.visible = shown && !shrunk
        h.updateMatrixWorld(true)
      }
    }
  }
}

/**
 * Blender-style move arrows on the selection, at the middle of the selected geoms' origins:
 * dragging an arrow moves them all along that axis, the arrows snapped to the grid, by setting
 * their XForm locations (one step of Edit > Undo).
 */
function MoveGizmo() {
  const ids = useStore((s) => s.tree?.active ?? NO_IDS)
  const key = ids.join(',')
  // Re-read the origins when any of the selected geoms changes.
  const revs = useStore((s) => ids.map((id) => s.meshRev[id] ?? 0).join(','))
  const [center, setCenter] = useState<[number, number, number] | null>(null)
  const obj = useRef<THREE.Group>(null)
  const controls = useRef<TransformControlsImpl>(null)
  const dragging = useRef(false)
  const start = useRef<[number, number, number]>(ZERO)

  const refresh = useCallback(() => {
    const sel = key ? key.split(',') : []
    if (!sel.length) return setCenter(null)
    Promise.all(sel.map((id) => api.geomOrigin(id))).then(
      (ps) => {
        if (dragging.current) return
        const c: [number, number, number] = [0, 0, 0]
        for (const p of ps) for (let k = 0; k < 3; k++) c[k] += p[k] / ps.length
        setCenter(c)
      },
      () => undefined,
    )
  }, [key])

  useEffect(() => {
    if (!dragging.current) refresh()
  }, [revs, refresh])

  const where = (): [number, number, number] => {
    const p = obj.current?.position
    return p ? [p.x, p.y, p.z] : start.current
  }
  const delta = (): [number, number, number] => {
    const p = where()
    const s = start.current
    return [p[0] - s[0], p[1] - s[1], p[2] - s[2]]
  }

  // While dragging, only the drawing moves; the geoms are moved when the arrow is let go,
  // since every move re-tessellates them.
  const onChange = () => {
    if (!dragging.current) return
    const r = useStore.getState().meshRev
    useMovePreview.setState({ ids, d: delta(), revs: Object.fromEntries(ids.map((id) => [id, r[id] ?? 0])) })
  }

  useEffect(() => {
    if (controls.current) keepArrowsOutward(controls.current)
  }, [key, center])

  if (!ids.length || !center) return null
  return (
    <>
      <group ref={obj} position={center} />
      <TransformControls
        ref={controls}
        object={obj as unknown as React.RefObject<THREE.Object3D>}
        mode="translate"
        space="world"
        translationSnap={MOVE_SNAP}
        size={0.9}
        onMouseDown={() => {
          gizmoPressedAt = performance.now()
          dragging.current = true
          start.current = where()
        }}
        onMouseUp={() => {
          gizmoPressedAt = performance.now()
          dragging.current = false
          const d = delta()
          if (d.every((v) => v === 0)) return useMovePreview.setState({ ids: [] })
          api.moveGeoms(ids, d).then(refresh, () => {
            useMovePreview.setState({ ids: [] })
            refresh()
          })
        }}
        onObjectChange={onChange}
      />
    </>
  )
}

/**
 * A blank geom: a circle at its origin (and each symmetric copy), gray, or red when it's
 * selected. Clicking one selects it, like clicking a surface.
 */
function BlankMarkers({ o }: { o: GlDrawObj }) {
  const name = useStore((s) => (o.id ? s.geomsById[o.id]?.name : undefined))
  const at: [number, number, number][] = []
  for (let i = 0; i + 2 < o.p.length; i += 3) at.push([o.p[i], o.p[i + 1], o.p[i + 2]])
  return (
    <>
      {at.map((p, i) => (
        <Html key={i} position={p} center zIndexRange={[5, 0]}>
          <div
            className={'blank-marker' + (o.sel ? ' sel' : '')}
            title={name}
            onClick={(e) => {
              e.stopPropagation()
              if (o.id) void api.pick(o.id, isToggleClick(e))
            }}
          />
        </Html>
      ))}
    </>
  )
}

/** A draw object's lines as segment end points (strips and loops joined up). */
function segmentPoints(o: GlDrawObj): [number, number, number][] {
  const n = o.p.length / 3
  const v = (i: number): [number, number, number] => [o.p[3 * i], o.p[3 * i + 1], o.p[3 * i + 2]]
  const out: [number, number, number][] = []
  if (o.t === 'lines') {
    for (let i = 0; i + 1 < n; i += 2) out.push(v(i), v(i + 1))
  } else {
    for (let i = 0; i + 1 < n; i++) out.push(v(i), v(i + 1))
    if (o.t === 'loop' && n > 2) out.push(v(n - 1), v(0))
  }
  return out
}

/**
 * What the main 3D view shows besides the model, mirrored from OpenVSP: the screens' draw
 * objects (the CG from Mass Properties, measures, CFD sources, ...), blank and hinge geoms,
 * and the selected geom's boxes, cross sections and skinning/blending tangents. Drawn on top,
 * like a highlight, so a CG inside the model or a section on its surface still shows.
 */
function Overlay() {
  const objs = useMirror((s) => s.overlay)
  const items = useMemo(
    () =>
      objs.map((o) => {
        if (o.t === 'blank') return { o, geo: null, seg: null }
        if (o.t === 'points' || o.t === 'tris') {
          const geo = new THREE.BufferGeometry()
          geo.setAttribute('position', new THREE.Float32BufferAttribute(o.p, 3))
          if (o.t === 'tris') geo.computeVertexNormals()
          return { o, geo, seg: null }
        }
        const seg = segmentPoints(o)
        // Dashes about an eighth of the average segment, so short vectors still read as dashed.
        let len = 0
        for (let i = 0; i + 1 < seg.length; i += 2) {
          const [a, b] = [seg[i], seg[i + 1]]
          len += Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2])
        }
        const dash = seg.length ? len / (seg.length / 2) / 8 : 1
        return { o, geo: null, seg, dash }
      }),
    [objs],
  )
  useEffect(() => () => items.forEach((i) => i.geo?.dispose()), [items])
  return (
    <>
      {items.map((it, k) => {
        const o = it.o
        if (o.t === 'blank') return <BlankMarkers key={k} o={o} />
        if (o.t === 'points')
          return (
            <points key={k} geometry={it.geo!} renderOrder={12}>
              <pointsMaterial color={o.pc} size={Math.max(4, o.ps || 6)} sizeAttenuation={false} depthTest={false} transparent />
            </points>
          )
        if (o.t === 'tris')
          return (
            <mesh key={k} geometry={it.geo!} renderOrder={11}>
              <meshBasicMaterial color={o.c} side={THREE.DoubleSide} depthTest={false} transparent />
            </mesh>
          )
        if (!it.seg?.length) return null
        return (
          <Line
            key={k}
            points={it.seg}
            segments
            color={o.c}
            lineWidth={Math.max(1, o.lw || 1)}
            dashed={!!o.dash}
            opacity={o.box === 1 ? BBOX_ALPHA : o.box === 2 ? SECTION_BOX_ALPHA : 1}
            dashSize={it.dash}
            gapSize={it.dash}
            depthTest={false}
            transparent
            renderOrder={10}
          />
        )
      })}
    </>
  )
}
