import { useEffect, useMemo, useRef, useState } from 'react'
import { openNewWindow } from '../newWindow'
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
  return (
    <div
      className={'m-float' + (win.closing ? ' closing' : '')}
      style={{ left: pos.x, top: pos.y, zIndex: win.modal ? 1000 + z : z }}
      onPointerDownCapture={() => z !== topZ && setZ(++topZ)}
    >
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
        <button className="ghost" title="Close" onClick={() => sendMirror({ op: 'close', win: win.id })}>
          ✕
        </button>
      </div>
      <MirrorWindow win={win} scale={scale} />
    </div>
  )
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
  // Ours, not OpenVSP's: another window with a model of its own (a separate engine).
  const file = root.find((n) => labelText(n.item.label) === 'File')
  file?.children.unshift({ item: { i: NEW_WINDOW, label: 'New Window', d: 1, flags: MENU.DIVIDER, sc: 0 }, children: [] })
  return root
}

const NEW_WINDOW = -1

// The 3D view is ours, so camera items in the View menu drive our viewport.
const VIEW_ITEMS: Record<string, ViewName> = {
  Top: 'top', Front: 'front', Left: 'left', 'Left Iso': 'leftiso', Bottom: 'bottom', Rear: 'rear', Right: 'right',
  'Right Iso': 'rightiso', 'Fit On Screen': 'fit', 'Fit All On Screen': 'fit', Center: 'fit', 'Center All': 'fit',
  'Reset All': 'leftiso',
}

function activate(bar: MWidget, node: MenuNode, path: string[]) {
  const label = labelText(node.item.label)
  if (node.item.i === NEW_WINDOW) {
    void openNewWindow()
    return
  }
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
      if (useMirror.getState().dialogs.length || useStore.getState().fileDialog) return
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
