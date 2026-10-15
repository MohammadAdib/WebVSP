import { Canvas, useFrame, type ThreeEvent } from '@react-three/fiber'
import { useCallback, useEffect, useMemo, useState } from 'react'
import * as THREE from 'three'
import { api } from '../api/client'
import { decodeMesh, gridLineIndices } from '../api/mesh'
import { NO_GEOMS, useStore } from '../store'
import { useViewer } from '../viewer/store'
import { useMirror } from '../mirror/store'
import { Navigation, useViewKeys, ViewTools } from './camera'

const SHADE = new THREE.Color('#a6a6a6')
const SHADE_SEL = new THREE.Color('#dedede')
const LINE_ON_SHADE = new THREE.Color('#202020')
const LINE_WIRE = new THREE.Color('#8f8f8f')
const LINE_SEL = new THREE.Color('#f2f2f2')

/** Per-frame view state written by CameraRig, read by meshes (avoids React re-renders). */
const view = {
  /** 1 when close, 0 when zoomed far out: dense wireframe on a tiny model reads as noise. */
  wireFade: 1,
}

interface Built {
  surfaces: { mesh: THREE.BufferGeometry; lines: THREE.BufferGeometry }[]
}

function buildGeometry(buf: ArrayBuffer): Built {
  return {
    surfaces: decodeMesh(buf).map((s) => {
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

  useEffect(() => {
    let cancelled = false
    api
      .mesh(id)
      .then((buf) => {
        if (!cancelled) setBuilt(buildGeometry(buf))
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
    void api.pick(id)
  }

  if (!built || mode === 'none') return null
  return (
    <group onClick={onClick}>
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
  return (
    <div className="viewport">
      <ViewTools onView={requestView} ortho={ortho} onToggleOrtho={toggleOrtho} />
      {/* Resize on every frame while the side panels slide: the default (debounced) resize
          stretches the old frame for the whole animation and then snaps. */}
      <Canvas dpr={[1, 2]} resize={{ scroll: false, debounce: 0 }}>
        <Scene />
      </Canvas>
    </div>
  )
}

/**
 * What the main 3D view shows besides the model: the screens' draw objects (the CG from Mass
 * Properties, measures, CFD sources, ...), mirrored from OpenVSP. Points are drawn on top,
 * so a CG inside the model still shows.
 */
function Overlay() {
  const objs = useMirror((s) => s.overlay)
  const items = useMemo(
    () =>
      objs.map((o) => {
        const geo = new THREE.BufferGeometry()
        let pts = o.p
        if (o.t === 'strip' || o.t === 'loop') {
          // As line segments: each consecutive pair (and back to the start for a loop).
          const seg: number[] = []
          const n = o.p.length / 3
          for (let i = 0; i + 1 < n; i++) seg.push(...o.p.slice(3 * i, 3 * i + 6))
          if (o.t === 'loop' && n > 2) seg.push(...o.p.slice(3 * (n - 1), 3 * n), ...o.p.slice(0, 3))
          pts = seg
        }
        geo.setAttribute('position', new THREE.Float32BufferAttribute(pts, 3))
        return { o, geo }
      }),
    [objs],
  )
  useEffect(() => () => items.forEach((i) => i.geo.dispose()), [items])
  return (
    <>
      {items.map(({ o, geo }, k) =>
        o.t === 'points' ? (
          <points key={k} geometry={geo} renderOrder={10}>
            <pointsMaterial color={o.pc} size={Math.max(4, o.ps || 6)} sizeAttenuation={false} depthTest={false} transparent />
          </points>
        ) : (
          <lineSegments key={k} geometry={geo}>
            <lineBasicMaterial color={o.c} />
          </lineSegments>
        ),
      )}
    </>
  )
}
