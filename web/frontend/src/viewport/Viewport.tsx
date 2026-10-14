import { GizmoHelper, GizmoViewport, OrbitControls, OrthographicCamera, PerspectiveCamera } from '@react-three/drei'
import { Canvas, useFrame, useThree, type ThreeEvent } from '@react-three/fiber'
import { useEffect, useMemo, useRef, useState } from 'react'
import * as THREE from 'three'
import type { OrbitControls as OrbitControlsImpl } from 'three-stdlib'
import { api } from '../api/client'
import { decodeMesh, gridLineIndices } from '../api/mesh'
import { NO_GEOMS, useStore, type ViewName } from '../store'

// OpenVSP is Z-up: X aft, Y starboard, Z up.
THREE.Object3D.DEFAULT_UP.set(0, 0, 1)

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

// VSP axes: X aft, Y starboard, Z up. Directions point from the model toward the camera.
// The tiny X offsets on top/bottom pick which way is "up" on screen: the nose (-X) faces
// down in both, like Blender's numpad 7.
type AbsoluteView = 'top' | 'bottom' | 'front' | 'rear' | 'left' | 'right' | 'leftiso' | 'rightiso'
const VIEW_DIRS: Record<AbsoluteView, THREE.Vector3> = {
  top: new THREE.Vector3(-1e-4, 0, 1).normalize(),
  bottom: new THREE.Vector3(1e-4, 0, -1).normalize(),
  front: new THREE.Vector3(-1, 0, 0),
  rear: new THREE.Vector3(1, 0, 0),
  left: new THREE.Vector3(0, -1, 0),
  right: new THREE.Vector3(0, 1, 0),
  leftiso: new THREE.Vector3(-1, -1, 1).normalize(),
  rightiso: new THREE.Vector3(-1, 1, 1).normalize(),
}

const FOV = 35
const TAN_HALF_FOV = Math.tan(THREE.MathUtils.degToRad(FOV / 2))
const ORBIT_STEP = THREE.MathUtils.degToRad(15)

/** Height of the visible region at the orbit target, for either camera type. */
function visibleHeight(camera: THREE.Camera, target: THREE.Vector3) {
  if ((camera as THREE.OrthographicCamera).isOrthographicCamera) {
    const o = camera as THREE.OrthographicCamera
    return (o.top - o.bottom) / o.zoom
  }
  return 2 * camera.position.distanceTo(target) * TAN_HALF_FOV
}

function CameraRig() {
  const { camera, controls, size } = useThree()
  const request = useStore((s) => s.viewRequest)
  const setBounds = useStore((s) => s.setBounds)
  const light = useRef<THREE.DirectionalLight>(null)
  // Bounding sphere of the model; drives clip planes and zoom limits.
  const sphere = useRef(new THREE.Sphere(new THREE.Vector3(), 10))
  // Last pose, carried over when switching between perspective and orthographic.
  const pose = useRef<{ dir: THREE.Vector3; target: THREE.Vector3; height: number } | null>(null)
  const lastCamera = useRef<THREE.Camera | null>(null)

  const ctl = controls as unknown as OrbitControlsImpl | null
  const isOrtho = (camera as THREE.OrthographicCamera).isOrthographicCamera === true

  /** Look at `target` from direction `dir`, showing `height` world units vertically. */
  const place = (dir: THREE.Vector3, target: THREE.Vector3, height: number) => {
    const r = sphere.current.radius
    if (isOrtho) {
      const o = camera as THREE.OrthographicCamera
      o.zoom = (o.top - o.bottom) / Math.max(height, 1e-6)
      o.position.copy(target).addScaledVector(dir, r * 4)
      o.updateProjectionMatrix()
    } else {
      camera.position.copy(target).addScaledVector(dir, height / (2 * TAN_HALF_FOV))
    }
    if (ctl) {
      ctl.target.copy(target)
      if (isOrtho) {
        const z = (camera as THREE.OrthographicCamera).zoom
        ctl.minZoom = z / 25
        ctl.maxZoom = z * 200
      } else {
        ctl.minDistance = r * 0.01
        ctl.maxDistance = r * 25
      }
      ctl.update()
    } else {
      camera.lookAt(target)
    }
  }

  // Switched camera type: keep looking at the same thing at the same scale. The orbit
  // controls are rebuilt for the new camera a render later, so wait until they're bound to it
  // (otherwise the new controls re-aim the camera from their defaults).
  const pendingPose = useRef<typeof pose.current>(null)
  useEffect(() => {
    if (lastCamera.current && lastCamera.current !== camera && pose.current) {
      pendingPose.current = pose.current
    }
    lastCamera.current = camera
    const p = pendingPose.current
    if (p && ctl && (ctl as unknown as { object: THREE.Camera }).object === camera) {
      pendingPose.current = null
      place(p.dir, p.target, p.height)
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [camera, controls])

  useEffect(() => {
    const target = ctl?.target ?? new THREE.Vector3()
    const name = request.name
    // Relative moves: rotate the camera about the orbit target.
    if (name.startsWith('orbit-') || name === 'flip') {
      const offset = camera.position.clone().sub(target)
      if (name === 'orbit-left' || name === 'orbit-right') {
        offset.applyAxisAngle(new THREE.Vector3(0, 0, 1), name === 'orbit-left' ? -ORBIT_STEP : ORBIT_STEP)
      } else if (name === 'orbit-up' || name === 'orbit-down') {
        const right = new THREE.Vector3().crossVectors(offset, camera.up).normalize()
        if (right.lengthSq() > 0) offset.applyAxisAngle(right, name === 'orbit-up' ? -ORBIT_STEP : ORBIT_STEP)
      } else {
        offset.negate() // Blender numpad 9: the opposite view
      }
      camera.position.copy(target).add(offset)
      ctl?.update()
      return
    }
    let cancelled = false
    api
      .bbox()
      .then((bb) => {
        if (cancelled) return
        const lo = new THREE.Vector3(...(bb?.min ?? [-5, -5, -5]))
        const hi = new THREE.Vector3(...(bb?.max ?? [5, 5, 5]))
        const center = lo.clone().add(hi).multiplyScalar(0.5)
        const radius = Math.max(hi.clone().sub(lo).length() / 2, 0.1)
        sphere.current.set(center, radius)
        const s = hi.clone().sub(lo)
        setBounds({ center: [center.x, center.y, center.z], size: [s.x, s.y, s.z], radius })
        const dir =
          name === 'fit' ? camera.position.clone().sub(target).normalize() : VIEW_DIRS[name as AbsoluteView].clone()
        if (dir.lengthSq() === 0) dir.copy(VIEW_DIRS.leftiso)
        // Fit the bounding sphere in the shorter side of the view.
        const aspect = size.width / Math.max(size.height, 1)
        const height = 2 * radius * 1.05 * Math.max(1, 1 / aspect)
        place(dir, center, height)
      })
      .catch(() => {})
    return () => {
      cancelled = true
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [request, setBounds])

  useFrame(() => {
    const target = ctl?.target ?? sphere.current.center
    const r = sphere.current.radius
    const height = visibleHeight(camera, target)
    pose.current = { dir: camera.position.clone().sub(target).normalize(), target: target.clone(), height }

    // Fit the clip planes tightly around the model every frame. Fixed planes lose depth
    // precision when zoomed far out, so shading and wire lines z-fight (jagged edges).
    const cam = camera as THREE.PerspectiveCamera | THREE.OrthographicCamera
    const d = camera.position.distanceTo(sphere.current.center)
    const far = d + r * 3 // the ground grid's corners are within ~1.8r of the center
    const near = isOrtho ? Math.max(d - r * 3, 1e-3) : Math.max(d - r * 1.5, far / 5000, r * 1e-4)
    if (Math.abs(cam.near - near) > near * 0.01 || Math.abs(cam.far - far) > far * 0.01) {
      cam.near = near
      cam.far = far
      cam.updateProjectionMatrix()
    }
    // Fade dense wireframe as the model gets small on screen (same measure for both cameras).
    const equivDist = height / (2 * TAN_HALF_FOV)
    view.wireFade = THREE.MathUtils.clamp((14 - equivDist / r) / 8, 0, 1)

    // Headlight: keep the key light at the camera so shading always reads well.
    if (light.current) {
      light.current.position.copy(camera.position)
      light.current.target.position.copy(target)
      light.current.target.updateMatrixWorld()
    }
  })

  return <directionalLight ref={light} intensity={2.2} />
}

function Scene() {
  const geoms = useStore((s) => s.tree?.geoms ?? NO_GEOMS)
  const shown = geoms.filter((g) => g.shown)
  const bounds = useStore((s) => s.bounds)
  const ortho = useStore((s) => s.ortho)
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
      {ortho ? (
        <OrthographicCamera makeDefault position={[-20, -24, 18]} zoom={20} />
      ) : (
        <PerspectiveCamera makeDefault fov={FOV} position={[-20, -24, 18]} />
      )}
      <CameraRig />
      <primitive object={grid} />
      {shown.map((g) => (
        <GeomMesh key={g.id} id={g.id} />
      ))}
      <OrbitControls makeDefault enableDamping={false} zoomToCursor />
      <GizmoHelper alignment="bottom-right" margin={[64, 64]}>
        <GizmoViewport
          axisColors={['#b5b5b5', '#8a8a8a', '#d9d9d9']}
          labelColor="#161616"
          axisHeadScale={0.9}
        />
      </GizmoHelper>
    </>
  )
}

/** Blender numpad navigation (top-row digits work too). Ctrl flips 1/3/7 to rear/left/bottom. */
function viewForKey(e: KeyboardEvent): ViewName | 'ortho' | null {
  const m = /^(?:Digit|Numpad)(\d)$/.exec(e.code)
  const k = m ? m[1] : e.key
  const ctrl = e.ctrlKey || e.metaKey
  switch (k) {
    case '1': return ctrl ? 'rear' : 'front'
    case '3': return ctrl ? 'left' : 'right'
    case '7': return ctrl ? 'bottom' : 'top'
    case '5': return 'ortho'
    case '9': return 'flip'
    case '4': return 'orbit-left'
    case '6': return 'orbit-right'
    case '8': return 'orbit-up'
    case '2': return 'orbit-down'
  }
  if (e.code === 'NumpadDecimal' || e.key === 'Home') return 'fit'
  return null
}

function useBlenderKeys() {
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const t = e.target as HTMLElement | null
      if (t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.tagName === 'SELECT' || t.isContentEditable)) return
      if (e.altKey || useStore.getState().fileDialog || document.querySelector('.modal-backdrop')) return
      const v = viewForKey(e)
      if (!v) return
      e.preventDefault()
      if (v === 'ortho') useStore.getState().toggleOrtho()
      else useStore.getState().requestView(v)
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [])
}

const VIEW_BUTTONS: { view: ViewName; label: string; key: string }[] = [
  { view: 'front', label: 'Front', key: '1' },
  { view: 'right', label: 'Side', key: '3' },
  { view: 'top', label: 'Top', key: '7' },
  { view: 'bottom', label: 'Bottom', key: 'Ctrl+7' },
  { view: 'leftiso', label: 'Iso', key: '' },
]

function ViewTools() {
  const requestView = useStore((s) => s.requestView)
  const ortho = useStore((s) => s.ortho)
  const toggleOrtho = useStore((s) => s.toggleOrtho)
  return (
    <div className="view-tools">
      {VIEW_BUTTONS.map((b) => (
        <button key={b.view} onClick={() => requestView(b.view)} title={b.key ? `${b.label} view (${b.key})` : `${b.label} view`}>
          {b.label}
        </button>
      ))}
      <button onClick={() => requestView('fit')} title="Fit all (Home / numpad .)">
        Fit
      </button>
      <span className="sep" />
      <button className="proj" onClick={toggleOrtho} title="Switch orthographic / perspective (5)">
        {ortho ? 'Ortho' : 'Persp'}
      </button>
    </div>
  )
}

export function Viewport() {
  useBlenderKeys()
  return (
    <div className="viewport">
      <ViewTools />
      <Canvas dpr={[1, 2]}>
        <Scene />
      </Canvas>
    </div>
  )
}
