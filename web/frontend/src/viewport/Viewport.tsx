import { GizmoHelper, GizmoViewport, OrbitControls } from '@react-three/drei'
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
const VIEW_DIRS: Record<Exclude<ViewName, 'fit'>, THREE.Vector3> = {
  top: new THREE.Vector3(1e-4, 0, 1).normalize(),
  bottom: new THREE.Vector3(1e-4, 0, -1).normalize(),
  front: new THREE.Vector3(-1, 0, 0),
  rear: new THREE.Vector3(1, 0, 0),
  left: new THREE.Vector3(0, -1, 0),
  right: new THREE.Vector3(0, 1, 0),
  leftiso: new THREE.Vector3(-1, -1, 1).normalize(),
  rightiso: new THREE.Vector3(-1, 1, 1).normalize(),
}

function CameraRig() {
  const { camera, controls } = useThree()
  const request = useStore((s) => s.viewRequest)
  const setBounds = useStore((s) => s.setBounds)
  const light = useRef<THREE.DirectionalLight>(null)
  // Bounding sphere of the model; drives clip planes and zoom limits.
  const sphere = useRef(new THREE.Sphere(new THREE.Vector3(), 10))

  useEffect(() => {
    const ctl = controls as unknown as OrbitControlsImpl | null
    let cancelled = false
    api
      .bbox()
      .then((bb) => {
        if (cancelled) return
        const lo = new THREE.Vector3(...(bb?.min ?? [-5, -5, -5]))
        const hi = new THREE.Vector3(...(bb?.max ?? [5, 5, 5]))
        const center = lo.clone().add(hi).multiplyScalar(0.5)
        const radius = Math.max(hi.clone().sub(lo).length() / 2, 0.1)
        const persp = camera as THREE.PerspectiveCamera
        const dist = radius / Math.sin(THREE.MathUtils.degToRad(persp.fov / 2)) * 1.05
        const dir =
          request.name === 'fit'
            ? camera.position.clone().sub(ctl?.target ?? new THREE.Vector3()).normalize()
            : VIEW_DIRS[request.name].clone()
        if (dir.lengthSq() === 0) dir.copy(VIEW_DIRS.leftiso)
        camera.position.copy(center.clone().add(dir.multiplyScalar(dist)))
        sphere.current.set(center, radius)
        const size = hi.clone().sub(lo)
        setBounds({ center: [center.x, center.y, center.z], size: [size.x, size.y, size.z], radius })
        if (ctl) {
          ctl.target.copy(center)
          ctl.minDistance = radius * 0.01
          ctl.maxDistance = radius * 25
          ctl.update()
        } else {
          camera.lookAt(center)
        }
      })
      .catch(() => {})
    return () => {
      cancelled = true
    }
  }, [request, camera, controls, setBounds])

  useFrame(() => {
    // Fit the clip planes tightly around the model every frame. Fixed planes lose depth
    // precision when zoomed far out, so shading and wire lines z-fight (jagged edges).
    const persp = camera as THREE.PerspectiveCamera
    const r = sphere.current.radius
    const d = camera.position.distanceTo(sphere.current.center)
    const far = d + r * 3 // the ground grid's corners are within ~1.8r of the center
    const near = Math.max(d - r * 1.5, far / 5000, r * 1e-4)
    if (Math.abs(persp.near - near) > near * 0.01 || Math.abs(persp.far - far) > far * 0.01) {
      persp.near = near
      persp.far = far
      persp.updateProjectionMatrix()
    }
    view.wireFade = THREE.MathUtils.clamp((14 - d / r) / 8, 0, 1)

    // Headlight: keep the key light at the camera so shading always reads well.
    if (light.current) {
      light.current.position.copy(camera.position)
      const ctl = controls as unknown as OrbitControlsImpl | null
      if (ctl) light.current.target.position.copy(ctl.target)
      light.current.target.updateMatrixWorld()
    }
  })

  return <directionalLight ref={light} intensity={2.2} />
}

function Scene() {
  const geoms = useStore((s) => s.tree?.geoms ?? NO_GEOMS)
  const shown = geoms.filter((g) => g.shown)
  const bounds = useStore((s) => s.bounds)
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

export function Viewport() {
  return (
    <div className="viewport">
      <Canvas camera={{ fov: 35, position: [-20, -24, 18] }} dpr={[1, 2]}>
        <Scene />
      </Canvas>
    </div>
  )
}
