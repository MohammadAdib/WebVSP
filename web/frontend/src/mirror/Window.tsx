import { useEffect, useMemo, useRef, useState } from 'react'
import { setSolverRunning, useSolverRun } from '../api/solverRun'
import { useStore, type ViewName } from '../store'
import { MENU, labelText, shortcutOf, shortcutText } from './fltk'
import { sendMirror, useMirror } from './store'
import type { MenuItem, MWidget, MWindow } from './types'
import { Children, WindowContext } from './Widgets'

/** One mirrored FLTK window drawn at its native layout, scaled by `scale`. */
export function MirrorWindow({ win, scale = 1 }: { win: MWindow; scale?: number }) {
  const ref = useRef<HTMLDivElement>(null)
  const ctx = useMemo(
    () => ({
      win: win.id,
      toWin: (cx: number, cy: number): [number, number] => {
        const r = ref.current!.getBoundingClientRect()
        return [Math.round((cx - r.left) / scale), Math.round((cy - r.top) / scale)]
      },
    }),
    [win.id, scale],
  )
  return (
    <div className="m-window-outer" style={{ width: win.w * scale, height: win.h * scale }}>
      <div ref={ref} className="m-window" style={{ width: win.w, height: win.h, transform: `scale(${scale})` }}>
        <WindowContext.Provider value={ctx}>
          <Children win={win.id} parent={0} px={0} py={0} />
        </WindowContext.Provider>
      </div>
    </div>
  )
}

let topZ = 20

/** The next stacking level for a window brought to the front (shared by all floating windows). */
export function nextZ() {
  return ++topZ
}

/** Whether z is the frontmost window's level. */
export const isTopZ = (z: number) => z === topZ

/** A floating, draggable frame around a mirrored window (screens and popups). */
export function FloatingWindow({ win, index, origin }: { win: MWindow; index: number; origin?: MWindow }) {
  // Open where VSP placed the window relative to its main window; cascade if unknown.
  const [pos, setPos] = useState(() => {
    const vw = window.innerWidth
    const vh = window.innerHeight - 60
    if (origin && win.x > -10000 && win.y > -10000) {
      const x = win.x - origin.x
      const y = win.y - origin.y
      return { x: Math.min(Math.max(0, x), Math.max(0, vw - win.w - 20)), y: Math.min(Math.max(0, y), Math.max(0, vh - 60)) }
    }
    return { x: 60 + (index % 6) * 28, y: 40 + (index % 6) * 28 }
  })
  const drag = useRef<{ dx: number; dy: number } | null>(null)
  const [z, setZ] = useState(() => ++topZ)
  const avail = Math.max(200, window.innerHeight - 120)
  const scale = Math.min(1, avail / win.h)

  // Resizing (windows FLTK lets resize): the real window is resized and re-lays itself out, never
  // below the size it was designed at. Sent at most every 50 ms while dragging.
  const sizing = useRef<{ edge: string; x: number; y: number; w: number; h: number; px: number; py: number; sent: number } | null>(null)
  const beginResize = (edge: string) => (e: React.PointerEvent) => {
    e.preventDefault()
    e.stopPropagation()
    e.currentTarget.setPointerCapture(e.pointerId)
    sizing.current = { edge, x: e.clientX, y: e.clientY, w: win.w, h: win.h, px: pos.x, py: pos.y, sent: 0 }
  }
  const resizeTo = (e: React.PointerEvent, final: boolean) => {
    const r = sizing.current
    if (!r) return
    const minW = win.dw ?? r.w
    const minH = win.dh ?? r.h
    const maxW = Math.max(minW, Math.floor((window.innerWidth - 8) / scale))
    const maxH = Math.max(minH, Math.floor(avail / scale))
    const dx = (e.clientX - r.x) / scale
    const dy = (e.clientY - r.y) / scale
    let w = r.w
    let h = r.h
    if (r.edge.includes('e')) w = r.w + dx
    if (r.edge.includes('w')) w = r.w - dx
    if (r.edge.includes('s')) h = r.h + dy
    if (r.edge.includes('n')) h = r.h - dy
    w = Math.round(Math.min(maxW, Math.max(minW, w)))
    h = Math.round(Math.min(maxH, Math.max(minH, h)))
    // Dragging the left or top edge keeps the opposite edge where it is.
    if (r.edge.includes('w') || r.edge.includes('n')) {
      setPos({
        x: r.edge.includes('w') ? Math.max(0, r.px + (r.w - w) * scale) : r.px,
        y: r.edge.includes('n') ? Math.max(0, r.py + (r.h - h) * scale) : r.py,
      })
    }
    const now = performance.now()
    if (final || now - r.sent > 50) {
      r.sent = now
      if (w !== win.w || h !== win.h) sendMirror({ op: 'resize', win: win.id, w, h })
    }
    if (final) sizing.current = null
  }
  return (
    <div
      className={'m-float' + (win.closing ? ' closing' : '')}
      style={{ left: pos.x, top: pos.y, zIndex: win.modal ? 1000 + z : z }}
      onPointerDownCapture={() => z !== topZ && setZ(++topZ)}
    >
      {win.rs &&
        !win.closing &&
        ['n', 's', 'e', 'w', 'ne', 'nw', 'se', 'sw'].map((edge) => (
          <div
            key={edge}
            className={`results-resize ${edge}`}
            onPointerDown={beginResize(edge)}
            onPointerMove={(e) => resizeTo(e, false)}
            onPointerUp={(e) => resizeTo(e, true)}
          />
        ))}
      <div
        className="m-float-title"
        onPointerDown={(e) => {
          // Capturing the pointer would steal the click from the close button.
          if ((e.target as HTMLElement).closest('button')) return
          e.currentTarget.setPointerCapture(e.pointerId)
          drag.current = { dx: e.clientX - pos.x, dy: e.clientY - pos.y }
        }}
        onPointerMove={(e) => {
          if (drag.current) setPos({ x: Math.max(0, e.clientX - drag.current.dx), y: Math.max(0, e.clientY - drag.current.dy) })
        }}
        onPointerUp={() => (drag.current = null)}
      >
        <span>{win.title || win.screen}</span>
        {win.screen === 'VSPAERO' && <SolverTimer />}
        <button className="ghost" title="Close" onClick={() => sendMirror({ op: 'close', win: win.id })}>
          ✕
        </button>
      </div>
      <MirrorWindow win={win} scale={scale} />
    </div>
  )
}

// A VSPAERO run lasts while its window's Kill Solver button is active (OpenVSP's own idea of it).
useMirror.subscribe((s, prev) => {
  if (s.widgets === prev.widgets) return
  let running = false
  for (const w of Object.values(s.widgets)) {
    if (w.k === 'button' && w.act && w.vis && labelText(w.label ?? '') === 'Kill Solver') {
      running = true
      break
    }
  }
  setSolverRunning(running)
})

/** The last VSPAERO run's time, in the VSPAERO window's title bar: counting while it runs, then
 *  how long it took and how it ended. */
function SolverTimer() {
  const { start, end, code } = useSolverRun()
  const [, tick] = useState(0)
  useEffect(() => {
    if (start === null || end !== null) return
    const t = window.setInterval(() => tick((n) => n + 1), 1000)
    return () => window.clearInterval(t)
  }, [start, end])
  if (start === null) return null
  const time = formatDuration((end ?? Date.now()) - start)
  if (end === null) return <span className="solver-timer running" title="VSPAERO is running">Running {time}</span>
  const how = code === 0 ? 'Done in' : code === 143 ? 'Stopped after' : 'Failed after'
  return <span className={'solver-timer' + (code === 0 ? '' : ' failed')} title="The last VSPAERO run">{how} {time}</span>
}

/** m:ss, or h:mm:ss from an hour. */
function formatDuration(ms: number) {
  const s = Math.max(0, Math.floor(ms / 1000))
  const two = (n: number) => String(n).padStart(2, '0')
  const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60)
  return h ? `${h}:${two(m)}:${two(s % 60)}` : `${m}:${two(s % 60)}`
}

// ------------------------------------------------------------------ menus

interface MenuNode {
  item: MenuItem
  children: MenuNode[]
}

function buildMenu(items: MenuItem[]): MenuNode[] {
  const root: MenuNode[] = []
  const stack: MenuNode[][] = [root]
  for (const item of items) {
    const node = { item, children: [] }
    const level = stack[Math.min(item.d, stack.length - 1)]
    if (!(item.flags & MENU.INVISIBLE)) level.push(node)
    stack.length = item.d + 1
    if (item.flags & MENU.SUBMENU) stack.push(node.children)
  }
  // Left out: menu items for things the page decides itself.
  for (const [menu, label] of HIDDEN_ITEMS) {
    const parent = root.find((n) => labelText(n.item.label) === menu)
    if (parent) parent.children = parent.children.filter((n) => labelText(n.item.label) !== label)
  }
  return root
}

/** OpenVSP menu items the page leaves out: File > Preferences only chooses between FLTK's and
 *  OpenVSP's file chooser, and the page always uses the browser's. */
const HIDDEN_ITEMS: [string, string][] = [['File', 'Preferences...']]

// The 3D view is ours, so camera items in the View menu drive our viewport.
const VIEW_ITEMS: Record<string, ViewName> = {
  Top: 'top', Front: 'front', Left: 'left', 'Left Iso': 'leftiso', Bottom: 'bottom', Rear: 'rear', Right: 'right',
  'Right Iso': 'rightiso', 'Fit On Screen': 'fit', 'Fit All On Screen': 'fit', Center: 'center', 'Center All': 'center',
  'Reset All': 'leftiso',
}

function activate(bar: MWidget, node: MenuNode, path: string[]) {
  const label = labelText(node.item.label)
  if (path[0] === 'View' && VIEW_ITEMS[label]) {
    useStore.getState().requestView(VIEW_ITEMS[label])
    return
  }
  sendMirror({ op: 'pick', id: bar.id, i: node.item.i })
}

function MenuList({ bar, nodes, path, close }: { bar: MWidget; nodes: MenuNode[]; path: string[]; close: () => void }) {
  const [sub, setSub] = useState<number | null>(null)
  return (
    <div className="menu-list">
      {nodes.map((n) => {
        const label = labelText(n.item.label)
        const inactive = (n.item.flags & MENU.INACTIVE) !== 0
        const isSub = (n.item.flags & MENU.SUBMENU) !== 0
        const toggle = (n.item.flags & (MENU.TOGGLE | MENU.RADIO)) !== 0
        return (
          <div key={n.item.i} className="menu-entry" onMouseEnter={() => setSub(isSub ? n.item.i : null)}>
            <button
              disabled={inactive}
              onClick={() => {
                if (isSub) return setSub(n.item.i)
                close()
                activate(bar, n, path)
              }}
            >
              <span>
                {toggle && <span className="menu-check">{n.item.flags & MENU.VALUE ? '✓' : ''}</span>}
                {label}
              </span>
              <span className="kbd">{isSub ? '▸' : shortcutText(n.item.sc)}</span>
            </button>
            {isSub && sub === n.item.i && (
              <div className="menu-sub">
                <MenuList bar={bar} nodes={n.children} path={[...path, label]} close={close} />
              </div>
            )}
            {n.item.flags & MENU.DIVIDER ? <hr /> : null}
          </div>
        )
      })}
    </div>
  )
}

/** The main window's menu bar (File/Edit/Window/View/Model/Analysis/Help). */
export function MirrorMenuBar() {
  const bar = useMirror((s) => {
    const main = Object.values(s.windows).find((w) => w.role === 'main')
    return main ? Object.values(s.widgets).find((w) => w.win === main.id && w.k === 'menubar') : undefined
  })
  const [open, setOpen] = useState<number | null>(null)
  const ref = useRef<HTMLDivElement>(null)
  const tree = useMemo(() => buildMenu((bar?.items ?? []) as MenuItem[]), [bar?.items])

  useEffect(() => {
    if (open === null) return
    const onDown = (e: PointerEvent) => {
      if (!ref.current?.contains(e.target as Node)) setOpen(null)
    }
    window.addEventListener('pointerdown', onDown)
    return () => window.removeEventListener('pointerdown', onDown)
  }, [open])

  // Menu shortcuts (Ctrl+S, F5, 'f', ...) go through the same items.
  useEffect(() => {
    if (!bar) return
    const flat: { node: MenuNode; path: string[] }[] = []
    const walk = (nodes: MenuNode[], path: string[]) =>
      nodes.forEach((n) => {
        flat.push({ node: n, path })
        walk(n.children, [...path, labelText(n.item.label)])
      })
    walk(tree, [])
    const onKey = (e: KeyboardEvent) => {
      const t = e.target as HTMLElement
      if (t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.tagName === 'SELECT')) return
      if (useMirror.getState().dialogs.length) return
      const sc = shortcutOf(e)
      if (!sc) return
      const hit = flat.find((f) => f.node.item.sc === sc && !(f.node.item.flags & (MENU.INACTIVE | MENU.SUBMENU)))
      if (hit) {
        e.preventDefault()
        activate(bar, hit.node, hit.path)
      }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [bar, tree])

  if (!bar) return null
  return (
    <div className="m-menubar" ref={ref}>
      {tree.map((n) => {
        const label = labelText(n.item.label)
        return (
          <div key={n.item.i} className="menu" onMouseEnter={() => open !== null && setOpen(n.item.i)}>
            <button className={'ghost' + (open === n.item.i ? ' on' : '')} onClick={() => setOpen(open === n.item.i ? null : n.item.i)}>
              {label}
            </button>
            {open === n.item.i && <MenuList bar={bar} nodes={n.children} path={[label]} close={() => setOpen(null)} />}
          </div>
        )
      })}
    </div>
  )
}
