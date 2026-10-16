import { Line } from '@react-three/drei'
import { useThree } from '@react-three/fiber'
import { useEffect, useRef } from 'react'
import * as THREE from 'three'
import { create } from 'zustand'
import { api } from '../api/client'
import { useStore } from '../store'

type Vec3 = [number, number, number]
const ZERO: Vec3 = [0, 0, 0]
const AXES: Vec3[] = [
  [1, 0, 0],
  [0, 1, 0],
  [0, 0, 1],
]
export const AXIS_NAMES = ['X', 'Y', 'Z']

/** Holding Ctrl while moving the mouse snaps a move to this (model units) and a rotation to
 *  ROTATE_SNAP degrees, as Blender's increment snapping does. */
const MOVE_SNAP = 1
const ROTATE_SNAP = 5

/**
 * What the 3D view draws moved or turned before the geoms themselves change: the selected geoms
 * while the move gizmo is dragged or G / R runs. Each geom is drawn this way until its new mesh
 * arrives (its meshRev passes the one recorded here).
 */
export interface Preview {
  ids: readonly string[]
  /** Moved by */
  d: Vec3
  /** Turned about a world axis (degrees), each geom about its own origin */
  rot: { axis: Vec3; angle: number; origins: Record<string, Vec3> } | null
  revs: Record<string, number>
}
export const usePreview = create<Preview>(() => ({ ids: [], d: ZERO, rot: null, revs: {} }))

/** The preview as a matrix for one geom (identity when it isn't moved). */
export function previewMatrix(p: Preview | null, id: string, out: THREE.Matrix4) {
  out.identity()
  if (!p) return out
  if (p.rot) {
    const o = p.rot.origins[id] ?? ZERO
    const r = new THREE.Matrix4().makeRotationAxis(new THREE.Vector3(...p.rot.axis).normalize(), THREE.MathUtils.degToRad(p.rot.angle))
    out.makeTranslation(o[0], o[1], o[2]).multiply(r).multiply(new THREE.Matrix4().makeTranslation(-o[0], -o[1], -o[2]))
  }
  if (p.d.some((v) => v !== 0)) out.premultiply(new THREE.Matrix4().makeTranslation(p.d[0], p.d[1], p.d[2]))
  return out
}

/** The meshRevs of these geoms now: the preview lasts until each one's mesh is newer. */
export const revsOf = (ids: readonly string[]) => {
  const r = useStore.getState().meshRev
  return Object.fromEntries(ids.map((id) => [id, r[id] ?? 0]))
}

/**
 * Blender's G (move) and R (rotate) on the selected geoms. The mouse drives it (moving in the
 * view's plane, or turning about the view's axis around the selection); X, Y or Z holds it to
 * that world axis (again: free); typing a number gives the exact distance or angle (along X when
 * no axis is held, about the view's axis for R). Enter or a click applies it, Esc or a right
 * click cancels. Rotation turns each geom about its own origin.
 */
interface Transform {
  mode: 'move' | 'rotate' | null
  /** Bumped per run: the mouse's start is taken again */
  run: number
  ids: string[]
  origins: Record<string, Vec3>
  /** Middle of the origins: what the mouse turns around, and the plane a free move slides in */
  pivot: Vec3
  axis: number | null
  /** Typed so far ('' when the mouse drives it) */
  typed: string
  /** From the mouse */
  mouseMove: Vec3
  mouseAngle: number
  /** Toward the viewer in the pane the mouse drives (rotation axis when none is held) */
  viewAxis: Vec3
}
export const useTransform = create<Transform>(() => ({
  mode: null,
  run: 0,
  ids: [],
  origins: {},
  pivot: ZERO,
  axis: null,
  typed: '',
  mouseMove: ZERO,
  mouseAngle: 0,
  viewAxis: [0, 0, 1],
}))

const typedValue = (t: string) => {
  const v = parseFloat(t)
  return Number.isFinite(v) ? v : t.startsWith('-') ? -0 : 0
}

/** The move or rotation as it stands. */
export function transformResult(s: Transform): { d: Vec3 } | { axis: Vec3; angle: number } {
  if (s.mode === 'move') {
    if (s.typed) {
      const a = AXES[s.axis ?? 0]
      const v = typedValue(s.typed)
      return { d: [a[0] * v, a[1] * v, a[2] * v] }
    }
    return { d: s.mouseMove }
  }
  const axis = s.axis === null ? s.viewAxis : AXES[s.axis]
  return { axis, angle: s.typed ? typedValue(s.typed) : s.mouseAngle }
}

// The preview follows the transform.
useTransform.subscribe((s, prev) => {
  if (!s.mode) return
  if (s === prev) return
  const r = transformResult(s)
  const revs = s.run !== prev.run ? revsOf(s.ids) : usePreview.getState().revs
  if ('d' in r) usePreview.setState({ ids: s.ids, d: r.d, rot: null, revs })
  else usePreview.setState({ ids: s.ids, d: ZERO, rot: { axis: r.axis, angle: r.angle, origins: s.origins }, revs })
})

/** Starts G or R on the selection, or on these geoms (none: nothing happens). */
export async function startTransform(mode: 'move' | 'rotate', geoms?: string[]) {
  const ids = geoms ?? useStore.getState().tree?.active ?? []
  if (!ids.length) return
  const ps = await Promise.all(ids.map((id) => api.geomOrigin(id)))
  const origins = Object.fromEntries(ids.map((id, i) => [id, ps[i]]))
  const pivot: Vec3 = [0, 0, 0]
  for (const p of ps) for (let k = 0; k < 3; k++) pivot[k] += p[k] / ps.length
  useTransform.setState((s) => ({ mode, run: s.run + 1, ids: [...ids], origins, pivot, axis: null, typed: '', mouseMove: ZERO, mouseAngle: 0 }))
}

export function cancelTransform() {
  if (!useTransform.getState().mode) return
  useTransform.setState({ mode: null })
  usePreview.setState({ ids: [], d: ZERO, rot: null })
}

export function applyTransform() {
  const s = useTransform.getState()
  if (!s.mode) return
  const r = transformResult(s)
  useTransform.setState({ mode: null })
  const clear = () => usePreview.setState({ ids: [], d: ZERO, rot: null })
  if ('d' in r) {
    if (r.d.every((v) => v === 0)) return clear()
    api.moveGeoms(s.ids, r.d).catch(clear)
  } else {
    if (r.angle === 0) return clear()
    api.rotateGeoms(s.ids, r.axis, r.angle).catch(clear)
  }
}

/** X / Y / Z: hold to that axis; the same again lets it go. */
export function setTransformAxis(axis: number) {
  useTransform.setState((s) => ({ axis: s.axis === axis ? null : axis }))
}

/** A key typed into the value: digits, '.', '-' (flips the sign), Backspace. */
export function typeTransform(key: string) {
  useTransform.setState((s) => {
    let t = s.typed
    if (key === 'Backspace') t = t.slice(0, -1)
    else if (key === '-') t = t.startsWith('-') ? t.slice(1) : '-' + t
    else if (key === '.' && t.includes('.')) return {}
    else t += key
    return { typed: t === '-' && key === 'Backspace' ? '' : t }
  })
}

/** Where the pointer is (G or R starts from it). */
const pointer = { x: 0, y: 0 }
if (typeof window !== 'undefined') {
  window.addEventListener('pointermove', (e) => ((pointer.x = e.clientX), (pointer.y = e.clientY)), { capture: true, passive: true })
}

/**
 * Turns the mouse into a move or rotation with the camera of the pane it runs in (the active
 * one). Lives in each pane's scene.
 */
export function TransformDriver({ active }: { active: boolean }) {
  const { camera, gl } = useThree()
  const mode = useTransform((s) => s.mode)
  const run = useTransform((s) => s.run)
  const start = useRef<{ run: number; x: number; y: number; plane0: THREE.Vector3 | null; lastAngle: number; turned: number } | null>(null)

  useEffect(() => {
    if (!active || !mode) return
    const pivot = new THREE.Vector3(...useTransform.getState().pivot)
    const toViewer = () => camera.getWorldDirection(new THREE.Vector3()).negate()
    useTransform.setState({ viewAxis: toViewer().toArray() as Vec3 })
    const rect = () => gl.domElement.getBoundingClientRect()
    // A point on screen (client pixels) to normalized device coordinates, and back.
    const ndc = (x: number, y: number) => {
      const r = rect()
      return new THREE.Vector2(((x - r.left) / r.width) * 2 - 1, -((y - r.top) / r.height) * 2 + 1)
    }
    const screenOf = (p: THREE.Vector3) => {
      const v = p.clone().project(camera)
      const r = rect()
      return new THREE.Vector2(r.left + ((v.x + 1) / 2) * r.width, r.top + ((1 - v.y) / 2) * r.height)
    }
    // A free move slides in the plane through the pivot facing the viewer.
    const planePoint = (x: number, y: number) => {
      const ray = new THREE.Raycaster()
      ray.setFromCamera(ndc(x, y), camera)
      const plane = new THREE.Plane().setFromNormalAndCoplanarPoint(toViewer(), pivot)
      return ray.ray.intersectPlane(plane, new THREE.Vector3())
    }
    const angleAt = (x: number, y: number) => {
      const c = screenOf(pivot)
      return Math.atan2(-(y - c.y), x - c.x)
    }
    if (start.current?.run !== run) {
      start.current = { run, x: pointer.x, y: pointer.y, plane0: planePoint(pointer.x, pointer.y), lastAngle: angleAt(pointer.x, pointer.y), turned: 0 }
    }
    const onMove = (e: PointerEvent) => {
      const s0 = start.current
      if (!s0) return
      const t = useTransform.getState()
      if (t.mode === 'move') {
        let d: THREE.Vector3
        if (t.axis !== null) {
          // Along the axis: the mouse's travel along the axis as drawn on screen.
          const a = new THREE.Vector3(...AXES[t.axis])
          const sa = screenOf(pivot.clone().add(a)).sub(screenOf(pivot))
          const travel = new THREE.Vector2(e.clientX - s0.x, e.clientY - s0.y)
          const len2 = sa.lengthSq()
          d = a.multiplyScalar(len2 > 1e-6 ? travel.dot(sa) / len2 : 0)
        } else {
          const p = planePoint(e.clientX, e.clientY)
          d = p && s0.plane0 ? p.sub(s0.plane0) : new THREE.Vector3()
        }
        const v = d.toArray() as Vec3
        if (e.ctrlKey) for (let k = 0; k < 3; k++) v[k] = Math.round(v[k] / MOVE_SNAP) * MOVE_SNAP
        useTransform.setState({ mouseMove: v })
      } else if (t.mode === 'rotate') {
        // Turned so far on screen around the pivot (it can go round more than once).
        const a = angleAt(e.clientX, e.clientY)
        let step = a - s0.lastAngle
        if (step > Math.PI) step -= 2 * Math.PI
        if (step < -Math.PI) step += 2 * Math.PI
        s0.lastAngle = a
        s0.turned += step
        // Counterclockwise on screen is about the axis toward the viewer; an axis pointing away
        // turns the other way.
        const facing = t.axis === null ? 1 : Math.sign(new THREE.Vector3(...AXES[t.axis]).dot(toViewer())) || 1
        let deg = THREE.MathUtils.radToDeg(s0.turned) * facing
        if (e.ctrlKey) deg = Math.round(deg / ROTATE_SNAP) * ROTATE_SNAP
        useTransform.setState({ mouseAngle: deg })
      }
    }
    window.addEventListener('pointermove', onMove)
    return () => window.removeEventListener('pointermove', onMove)
  }, [active, mode, run, camera, gl])

  return null
}

/** The locked axis's colour, Blender's. */
export const AXIS_COLORS = ['#ff3352', '#8bdc00', '#2890ff']
/** How far the locked axis line reaches each way from the selection (model units): off screen. */
const AXIS_LINE_REACH = 1e5

/** While G / R is locked to an axis: that axis through the selection, in its colour. */
export function TransformAxisLine() {
  const axis = useTransform((s) => (s.mode ? s.axis : null))
  const pivot = useTransform((s) => s.pivot)
  if (axis === null) return null
  const a = AXES[axis]
  const end = (sign: number): Vec3 => [pivot[0] + sign * a[0] * AXIS_LINE_REACH, pivot[1] + sign * a[1] * AXIS_LINE_REACH, pivot[2] + sign * a[2] * AXIS_LINE_REACH]
  return <Line points={[end(-1), end(1)]} color={AXIS_COLORS[axis]} lineWidth={1.5} depthTest={false} transparent opacity={0.85} renderOrder={13} raycast={() => null} />
}

/** What G / R is doing, at the top of the 3D view (like Blender's header text). */
export function TransformHud() {
  const s = useTransform()
  if (!s.mode) return null
  const r = transformResult(s)
  // The axis it's held to (a typed move with none goes along X), in the axis's colour.
  const k = s.axis ?? ('d' in r && s.typed ? 0 : null)
  const axis = k === null ? null : <span style={{ color: AXIS_COLORS[k], fontWeight: 600 }}>{AXIS_NAMES[k]}</span>
  const value =
    'd' in r ? (
      k !== null ? (
        <>{axis} {s.typed || fmt(r.d[k])}</>
      ) : (
        `D ${fmt(r.d[0])}, ${fmt(r.d[1])}, ${fmt(r.d[2])}`
      )
    ) : (
      <>
        {s.typed || fmt(r.angle)}°{axis && <> about {axis}</>}
      </>
    )
  return (
    <div className="transform-hud" role="status">
      <b>{s.mode === 'move' ? 'Move' : 'Rotate'}</b> <span>{value}</span>
      <span className="hint">X Y Z axis · type a value · Enter or click to apply · Esc or right-click to cancel · Ctrl snaps</span>
    </div>
  )
}

const fmt = (v: number) => (Math.abs(v) < 1e-9 ? '0' : Number(v.toFixed(3)).toString())
