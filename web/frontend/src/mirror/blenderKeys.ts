import { useEffect } from 'react'
import { api } from '../api/client'
import { useStore } from '../store'
import { useViewer } from '../viewer/store'
import { applyTransform, cancelTransform, setTransformAxis, startTransform, typeTransform, useTransform } from '../viewport/transform'
import { askDelete } from './DeleteConfirm'
import { labelText } from './fltk'
import { sendMirror, useMirror } from './store'

/**
 * Blender's keys for the selection, done with the Geom Browser's own buttons where it has them,
 * so they act on every selected geom exactly as clicking them would:
 *   A        select all            (Sel All)
 *   Alt+A    deselect all          (a pick of nothing, like clicking empty space)
 *   H        hide the selected     (NoShow)
 *   Shift+H  hide the unselected   (Show Only)
 *   Alt+H    show everything; the geoms it reveals join the selection, as in Blender
 *   Z        the selected geoms wireframe, or shaded if they all are already
 *   Shift+Z  the same for every geom (judged by the shown ones)
 *   X, Del   delete the selection, after asking (then the Geom Browser's Delete, which also
 *            asks about clones)
 *   Shift+D  duplicate the selection in place, then move the copies (G); Esc leaves them there
 *   G, R     move or rotate the selection (viewport/transform.tsx), with the keys and clicks
 *            below while it runs
 * They listen ahead of everything else (capture), so while G / R runs its keys (X, digits...)
 * don't also reach the view keys or OpenVSP's menu shortcuts. Plain R is taken from OpenVSP's
 * View > Set Rotation Center, which has nothing to act on in the page.
 */
export function useBlenderKeys() {
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (useTransform.getState().mode) return transformKey(e)
      const t = e.target as HTMLElement | null
      if (t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.tagName === 'SELECT' || t.isContentEditable)) return
      if (e.ctrlKey || e.metaKey || e.repeat || !viewKeysFree()) return
      if (e.key === 'Delete' && !e.altKey && !e.shiftKey) {
        e.preventDefault()
        e.stopPropagation()
        return askDelete(() => clickBrowser('Delete'))
      }
      // e.code: with Alt (Option on a Mac) e.key is another character.
      const key = { KeyA: 'a', KeyH: 'h', KeyZ: 'z', KeyG: 'g', KeyR: 'r', KeyD: 'd', KeyX: 'x' }[e.code]
      if (!key) return
      let action: (() => unknown) | null = null
      if (key === 'a') action = e.shiftKey ? null : e.altKey ? () => api.pick('') : () => clickBrowser('Sel All')
      else if (key === 'h') action = e.altKey ? (e.shiftKey ? null : showAll) : () => clickBrowser(e.shiftKey ? 'Show Only' : 'NoShow')
      else if (key === 'z') action = e.altKey ? null : () => toggleWire(!e.shiftKey)
      else if (key === 'd') action = e.shiftKey && !e.altKey ? duplicate : null
      else if (key === 'x') action = e.shiftKey || e.altKey ? null : () => askDelete(() => clickBrowser('Delete'))
      else if (!e.altKey && !e.shiftKey) action = () => startTransform(key === 'g' ? 'move' : 'rotate')
      if (!action) return
      e.preventDefault()
      e.stopPropagation()
      void action()
    }
    // While G / R runs, a click applies it and a right click cancels; neither reaches the view
    // (no orbit, no selection).
    const onPointer = (e: PointerEvent) => {
      if (!useTransform.getState().mode) return
      if (e.button !== 0 && e.button !== 2) return
      e.preventDefault()
      e.stopPropagation()
      if (e.button === 0) applyTransform()
      else cancelTransform()
      swallowUntil = performance.now() + 600
    }
    const swallow = (e: Event) => {
      if (performance.now() < swallowUntil || useTransform.getState().mode) {
        e.preventDefault()
        e.stopPropagation()
      }
    }
    window.addEventListener('keydown', onKey, true)
    window.addEventListener('pointerdown', onPointer, true)
    for (const type of ['pointerup', 'click', 'contextmenu', 'mousedown', 'mouseup']) window.addEventListener(type, swallow, true)
    return () => {
      window.removeEventListener('keydown', onKey, true)
      window.removeEventListener('pointerdown', onPointer, true)
      for (const type of ['pointerup', 'click', 'contextmenu', 'mousedown', 'mouseup']) window.removeEventListener(type, swallow, true)
    }
  }, [])
}

/** Until when the clicks after applying or cancelling G / R are kept from the page. */
let swallowUntil = 0

/** The 3D view's keys are free: no prompt open and the results viewer (which covers it) shut. */
const viewKeysFree = () => !useMirror.getState().dialogs.length && !document.querySelector('.modal-backdrop') && !useViewer.getState().open

/** A key while G / R runs: X / Y / Z hold an axis, digits type the value, Enter applies, Esc
 *  cancels, G / R switch to the other. Every other key is kept from the page meanwhile. */
function transformKey(e: KeyboardEvent) {
  if (['Shift', 'Control', 'Alt', 'Meta'].includes(e.key)) return
  e.preventDefault()
  e.stopPropagation()
  const k = e.key
  if (k === 'Escape') cancelTransform()
  else if (k === 'Enter') applyTransform()
  else if (/^[xyz]$/i.test(k) && !e.ctrlKey) setTransformAxis('xyz'.indexOf(k.toLowerCase()))
  else if (/^[0-9.-]$/.test(k) || k === 'Backspace') typeTransform(k)
  else if (/^[gr]$/i.test(k) && !e.repeat) {
    const mode = k.toLowerCase() === 'g' ? 'move' : 'rotate'
    if (useTransform.getState().mode !== mode) {
      cancelTransform()
      void startTransform(mode)
    }
  }
}

/** The Geom Browser's button with this label (visible and active). "Show Only" and "Show" are
 *  there twice (the second pair, lower down, acts on a set): the top one is the selected geoms'. */
function browserButton(label: string) {
  const s = useMirror.getState()
  const browser = Object.values(s.windows).find((w) => w.role === 'browser')
  if (!browser) return undefined
  return Object.values(s.widgets)
    .filter((w) => w.win === browser.id && w.k === 'button' && w.vis && w.act && labelText(w.label ?? '') === label)
    .sort((a, b) => a.y - b.y)[0]
}

function clickBrowser(label: string) {
  const button = browserButton(label)
  if (button) sendMirror({ op: 'click', id: button.id })
}

/** Shift+D: copies of the selection in place, selected and following the mouse (G). */
async function duplicate() {
  const sel = useStore.getState().tree?.active ?? []
  if (!sel.length) return
  const { ids } = await api.duplicateGeoms(sel)
  if (ids.length) await startTransform('move', ids)
}

async function showAll() {
  const tree = useStore.getState().tree
  if (!tree) return
  const active = new Set(tree.active)
  const hidden = tree.geoms.filter((g) => !g.shown && !active.has(g.id))
  // Add the hidden geoms to the selection (each pick waits, so Show sees them all), then show the
  // selection.
  for (const g of hidden) await api.pick(g.id, true)
  clickBrowser('Show')
}

const WIRE = 0
const SHADE = 2
const NONE = 4

/** Z (the selection) and Shift+Z (every geom): wireframe, or shaded when they're all wireframe
 *  already. Shift+Z judges by the shown geoms. Geoms drawn as None stay that way. */
async function toggleWire(selected: boolean) {
  const tree = useStore.getState().tree
  if (!tree) return
  const active = new Set(tree.active)
  const geoms = tree.geoms.filter((g) => g.drawType !== NONE && (selected ? active.has(g.id) : true))
  const judged = selected ? geoms : geoms.filter((g) => g.shown)
  if (!geoms.length) return
  const allWire = (judged.length ? judged : geoms).every((g) => g.drawType === WIRE)
  await api.setDrawType(geoms.map((g) => g.id), allWire ? SHADE : WIRE)
}
