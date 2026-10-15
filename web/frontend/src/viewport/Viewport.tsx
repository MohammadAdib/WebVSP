import { Html, Line, TransformControls } from '@react-three/drei'
import { Canvas, useFrame, type ThreeEvent } from '@react-three/fiber'
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
  surfaces: { mesh: THREE.BufferGeometry; lines: THREE.BufferGeometry }[]
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
      lines.setAttribute('position', pos)
      lines.setIndex(new THREE.BufferAttribute(gridLineIndices(s.nu, s.nw), 1))
      return { mesh, lines }
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

  if (mode === 'hidden') {
    // Hidden-line: surfaces in the background color occlude the lines behind them.
    shadeMat.color.set('#161616')
    shadeMat.emissive.set('#161616')
  } else {
    shadeMat.color.copy(selected ? SHADE_SEL : SHADE)
    shadeMat.emissive.set(selected ? '#262626' : '#000000')
  }
  lineMat.color.copy(selected ? LINE_SEL : mode === 'shaded' ? LINE_ON_SHADE : LINE_WIRE)
  useFrame(() => {
    // Pure wire mode keeps its lines; over shading they fade out as the model gets small.
    // Shaded geoms show their wireframe only when selected, as in the FLTK view.
    const base = mode === 'shaded' ? (selected ? 0.9 : 0) : 1
    const fade = mode === 'shaded' ? view.wireFade : 1
    lineMat.opacity = base * fade
    lineMat.visible = lineMat.opacity > 0.02
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
          {(mode === 'shaded' || mode === 'hidden') && <mesh geometry={s.mesh} material={shadeMat} />}
          <lineSegments geometry={s.lines} material={lineMat} />
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

function Scene() {
  const geoms = useStore((s) => s.tree?.geoms ?? NO_GEOMS)
  const shown = geoms.filter((g) => g.shown)
  const bounds = useStore((s) => s.bounds)
  const ortho = useStore((s) => s.ortho)
  const request = useStore((s) => s.viewRequest)
  const setBounds = useStore((s) => s.setBounds)
  const grid = useMemo(() => {
    // Square 16x16 grid, 1.25x the model's larger plan dimension (X length or Y span),
    // centered under the model on the Z=0 plane.
    const span = bounds ? 1.25 * Math.max(bounds.size[0], bounds.size[1]) : 20
    const g = new THREE.GridHelper(span || 20, 16, '#3a3a3a', '#262626')
    g.rotation.x = Math.PI / 2
    if (bounds) g.position.set(bounds.center[0], bounds.center[1], 0)
    return g
  }, [bounds])
  useEffect(() => () => {
    grid.geometry.dispose()
    ;(grid.material as THREE.Material).dispose()
  }, [grid])

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
      <primitive object={grid} />
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
  // The keys are the results viewer's while it's open (it covers this view).
  const active = useCallback(
    () => !useStore.getState().fileDialog && !document.querySelector('.modal-backdrop') && !useViewer.getState().open,
    [],
  )
  useViewKeys(requestView, toggleOrtho, active)
  const down = useRef<[number, number] | null>(null)
  return (
    <div className="viewport">
      <ViewTools onView={requestView} ortho={ortho} onToggleOrtho={toggleOrtho} />
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
