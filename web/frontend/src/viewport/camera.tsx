import { GizmoHelper, GizmoViewport, OrbitControls, OrthographicCamera, PerspectiveCamera } from '@react-three/drei'
import { useFrame, useThree } from '@react-three/fiber'
import { useEffect, useRef } from 'react'
import * as THREE from 'three'
import type { OrbitControls as OrbitControlsImpl } from 'three-stdlib'
import type { Layout, ViewName } from '../store'

/**
 * 3D navigation shared by the main view and the VSPAERO results viewer: the camera (perspective
 * or orthographic, keeping the view when switching), orbit/pan/zoom-to-cursor with the mouse,
 * the view presets and Blender-style keys, fitting, clip planes and a headlight.
 */

// OpenVSP is Z-up: X aft, Y starboard, Z up.
THREE.Object3D.DEFAULT_UP.set(0, 0, 1)

// Directions point from the model toward the camera. The tiny X offsets on top/bottom pick
// which way is "up" on screen: the nose (-X) faces down in both, like Blender's numpad 7.
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

export const FOV = 35
export const TAN_HALF_FOV = Math.tan(THREE.MathUtils.degToRad(FOV / 2))
const ORBIT_STEP = THREE.MathUtils.degToRad(15)

export interface ViewRequest {
  name: ViewName
  n: number
}

export interface Box {
  min: [number, number, number]
  max: [number, number, number]
}

/** Height of the visible region at the orbit target, for either camera type. */
function visibleHeight(camera: THREE.Camera, target: THREE.Vector3) {
  if ((camera as THREE.OrthographicCamera).isOrthographicCamera) {
    const o = camera as THREE.OrthographicCamera
    return (o.top - o.bottom) / o.zoom
  }
  return 2 * camera.position.distanceTo(target) * TAN_HALF_FOV
}

interface RigProps {
  request: ViewRequest
  /** The model's bounding box (fetched when a view is requested). */
  getBounds: () => Promise<Box | null>
  onBounds?: (b: { center: [number, number, number]; size: [number, number, number]; radius: number }) => void
  /** Each frame: how far away the model looks, in model radii (e.g. to fade wireframe). */
  onFrame?: (distanceInRadii: number) => void
  /** Depth kept visible around the model, in model radii (more for long wakes). Default 3. */
  clipScale?: number
}

function CameraRig({ request, getBounds, onBounds, onFrame, clipScale = 3 }: RigProps) {
  const { camera, controls, size } = useThree()
  const light = useRef<THREE.DirectionalLight>(null)
  // Bounding sphere of the model; drives clip planes and zoom limits.
  const sphere = useRef(new THREE.Sphere(new THREE.Vector3(), 10))
  // Last pose, carried over when switching between perspective and orthographic.
  const pose = useRef<{ dir: THREE.Vector3; target: THREE.Vector3; height: number } | null>(null)
  const lastCamera = useRef<THREE.Camera | null>(null)
  // Cameras that have been framed: only their view carries over to a new camera (not the
  // renderer's default camera, which is current for a moment before ours is installed).
  const framed = useRef(new WeakSet<THREE.Camera>())

  const ctl = controls as unknown as OrbitControlsImpl | null
  const isOrtho = (camera as THREE.OrthographicCamera).isOrthographicCamera === true

  /** Look at `target` from direction `dir`, showing `height` world units vertically. */
  const place = (dir: THREE.Vector3, target: THREE.Vector3, height: number) => {
    framed.current.add(camera)
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
    if (lastCamera.current && lastCamera.current !== camera && pose.current && framed.current.has(lastCamera.current)) {
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

  // A view request waits for the mouse controls (they're created after the camera on the
  // first render); otherwise the first fit is lost and the camera starts inside the model.
  const applied = useRef(-1)
  const bound = !!ctl && (ctl as unknown as { object: THREE.Camera }).object === camera
  useEffect(() => {
    if (!ctl || !bound || applied.current === request.n) return
    applied.current = request.n
    const target = ctl.target
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
      ctl.update()
      return
    }
    let cancelled = false
    getBounds()
      .then((bb) => {
        if (cancelled) return
        const lo = new THREE.Vector3(...(bb?.min ?? [-5, -5, -5]))
        const hi = new THREE.Vector3(...(bb?.max ?? [5, 5, 5]))
        const center = lo.clone().add(hi).multiplyScalar(0.5)
        const radius = Math.max(hi.clone().sub(lo).length() / 2, 0.1)
        sphere.current.set(center, radius)
        const s = hi.clone().sub(lo)
        onBounds?.({ center: [center.x, center.y, center.z], size: [s.x, s.y, s.z], radius })
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
  }, [request, controls, camera, bound])

  useFrame(() => {
    const target = ctl?.target ?? sphere.current.center
    const r = sphere.current.radius
    const height = visibleHeight(camera, target)
    pose.current = { dir: camera.position.clone().sub(target).normalize(), target: target.clone(), height }

    // Fit the clip planes tightly around the model every frame. Fixed planes lose depth
    // precision when zoomed far out, so shading and wire lines z-fight (jagged edges).
    const cam = camera as THREE.PerspectiveCamera | THREE.OrthographicCamera
    const d = camera.position.distanceTo(sphere.current.center)
    const far = d + r * clipScale // the ground grid reaches ~2.8r from the center (faded out there)
    const near = isOrtho ? Math.max(d - r * clipScale, 1e-3) : Math.max(d - r * (clipScale / 2), far / 5000, r * 1e-4)
    if (Math.abs(cam.near - near) > near * 0.01 || Math.abs(cam.far - far) > far * 0.01) {
      cam.near = near
      cam.far = far
      cam.updateProjectionMatrix()
    }
    onFrame?.(height / (2 * TAN_HALF_FOV) / r)

    // Headlight: keep the key light at the camera so shading always reads well.
    if (light.current) {
      light.current.position.copy(camera.position)
      light.current.target.position.copy(target)
      light.current.target.updateMatrixWorld()
    }
  })

  return <directionalLight ref={light} intensity={2.2} />
}

/** The camera, its rig, the mouse controls and the axes gizmo. Put inside a <Canvas>. */
export function Navigation({ ortho, ...rig }: RigProps & { ortho: boolean }) {
  return (
    <>
      {ortho ? (
        <OrthographicCamera makeDefault position={[-20, -24, 18]} zoom={20} />
      ) : (
        <PerspectiveCamera makeDefault fov={FOV} position={[-20, -24, 18]} />
      )}
      <CameraRig {...rig} />
      <OrbitControls makeDefault enableDamping={false} zoomToCursor />
      <GizmoHelper alignment="bottom-right" margin={[64, 64]}>
        <GizmoViewport axisColors={['#b5b5b5', '#8a8a8a', '#d9d9d9']} labelColor="#161616" axisHeadScale={0.9} />
      </GizmoHelper>
    </>
  )
}

/**
 * Blender numpad navigation (top-row digits work too). Ctrl flips 1/3/7 to rear/left/bottom.
 * Home, numpad . and F fit the model.
 */
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
  if ((e.key === 'f' || e.key === 'F') && !ctrl) return 'fit'
  return null
}

/** Navigation keys, while `active()` says this view has them. */
export function useViewKeys(onView: (v: ViewName) => void, onToggleOrtho: () => void, active: () => boolean) {
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const t = e.target as HTMLElement | null
      if (t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.tagName === 'SELECT' || t.isContentEditable)) return
      if (e.altKey || !active()) return
      const v = viewForKey(e)
      if (!v) return
      e.preventDefault()
      if (v === 'ortho') onToggleOrtho()
      else onView(v)
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [onView, onToggleOrtho, active])
}

const VIEW_BUTTONS: { view: ViewName; label: string; key: string }[] = [
  { view: 'front', label: 'Front', key: '1' },
  { view: 'right', label: 'Side', key: '3' },
  { view: 'top', label: 'Top', key: '7' },
  { view: 'bottom', label: 'Bottom', key: 'Ctrl+7' },
  { view: 'leftiso', label: 'Iso', key: '' },
]

/** Box whose back face shrinks into the distance: perspective projection. */
function PerspIcon() {
  return (
    <svg width="18" height="18" viewBox="0 0 18 18" fill="none" stroke="currentColor" strokeWidth="1.3" strokeLinejoin="round">
      <rect x="1.5" y="6.5" width="10" height="10" />
      <rect x="10.5" y="1.5" width="5" height="5" strokeOpacity="0.7" />
      <path d="M1.5 6.5 L10.5 1.5 M11.5 6.5 L15.5 1.5 M11.5 16.5 L15.5 6.5" strokeOpacity="0.7" />
    </svg>
  )
}

/** Box with equal front and back faces and parallel edges: orthographic projection. */
function OrthoIcon() {
  return (
    <svg width="18" height="18" viewBox="0 0 18 18" fill="none" stroke="currentColor" strokeWidth="1.3" strokeLinejoin="round">
      <rect x="1.5" y="6.5" width="10" height="10" />
      <rect x="6.5" y="1.5" width="10" height="10" strokeOpacity="0.7" />
      <path d="M1.5 6.5 L6.5 1.5 M11.5 6.5 L16.5 1.5 M11.5 16.5 L16.5 11.5 M1.5 16.5 L6.5 11.5" strokeOpacity="0.7" />
    </svg>
  )
}

/** A grid of lines: the ground grid switch. */
function GridIcon() {
  return (
    <svg width="18" height="18" viewBox="0 0 18 18" fill="none" stroke="currentColor" strokeWidth="1.2">
      <path d="M1.5 6 H16.5 M1.5 12 H16.5 M6 1.5 V16.5 M12 1.5 V16.5" />
      <rect x="1.5" y="1.5" width="15" height="15" strokeOpacity="0.6" />
    </svg>
  )
}

/** The 3D view's split layouts: one view, two side by side, two stacked, four. */
function LayoutIcon({ layout }: { layout: Layout }) {
  const panes = {
    one: [[1.5, 1.5, 15, 15]],
    twoH: [[1.5, 1.5, 7, 15], [9.5, 1.5, 7, 15]],
    twoV: [[1.5, 1.5, 15, 7], [1.5, 9.5, 15, 7]],
    four: [[1.5, 1.5, 7, 7], [9.5, 1.5, 7, 7], [1.5, 9.5, 7, 7], [9.5, 9.5, 7, 7]],
  }[layout]
  return (
    <svg width="18" height="18" viewBox="0 0 18 18" fill="none" stroke="currentColor" strokeWidth="1.2">
      {panes.map(([x, y, w, h], i) => (
        <rect key={i} x={x} y={y} width={w} height={h} rx="0.8" />
      ))}
    </svg>
  )
}

const LAYOUTS: { layout: Layout; label: string }[] = [
  { layout: 'twoH', label: 'Two side by side' },
  { layout: 'twoV', label: 'Two stacked' },
  { layout: 'four', label: 'Four views' },
]

/** The view preset buttons, the projection switch and (if given) the ground grid switch and
 *  the split layouts. */
export function ViewTools({
  onView,
  ortho,
  onToggleOrtho,
  grid,
  onToggleGrid,
  layout,
  onLayout,
}: {
  onView: (v: ViewName) => void
  ortho: boolean
  onToggleOrtho: () => void
  grid?: boolean
  onToggleGrid?: () => void
  layout?: Layout
  onLayout?: (l: Layout) => void
}) {
  return (
    <div className="view-tools">
      {VIEW_BUTTONS.map((b) => (
        <button key={b.view} onClick={() => onView(b.view)} title={b.key ? `${b.label} view (${b.key})` : `${b.label} view`}>
          {b.label}
        </button>
      ))}
      <button onClick={() => onView('fit')} title="Fit all (F / Home / numpad .)">
        Fit
      </button>
      <span className="sep" />
      <button
        className="proj"
        onClick={onToggleOrtho}
        title={ortho ? 'Orthographic — click for perspective (5)' : 'Perspective — click for orthographic (5)'}
        aria-label={ortho ? 'Orthographic view' : 'Perspective view'}
      >
        {ortho ? <OrthoIcon /> : <PerspIcon />}
      </button>
      {onToggleGrid && (
        <button
          className={'proj' + (grid ? '' : ' off')}
          onClick={onToggleGrid}
          title={grid ? 'Grid shown — click to hide' : 'Grid hidden — click to show'}
          aria-label={grid ? 'Hide grid' : 'Show grid'}
          aria-pressed={!!grid}
        >
          <GridIcon />
        </button>
      )}
      {onLayout && (
        <>
          <span className="sep" />
          {LAYOUTS.map((l) => (
            <button
              key={l.layout}
              className={'proj' + (layout === l.layout ? ' on' : ' off')}
              // Clicking the layout in use goes back to one view.
              onClick={() => onLayout(layout === l.layout ? 'one' : l.layout)}
              title={layout === l.layout ? `${l.label} — click for one view` : l.label}
              aria-label={l.label}
              aria-pressed={layout === l.layout}
            >
              <LayoutIcon layout={l.layout} />
            </button>
          ))}
        </>
      )}
    </div>
  )
}
