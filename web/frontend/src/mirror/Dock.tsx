import { useEffect, useRef, useState, type ReactNode } from 'react'
import { sendMirror } from './store'
import type { MWindow } from './types'
import { MirrorWindow } from './Window'

/** Height available to the side panels (between the top bar and the status bar). */
export function usePanelHeight(ref: React.RefObject<HTMLElement | null>) {
  const [h, setH] = useState(0)
  useEffect(() => {
    const el = ref.current
    if (!el) return
    const ro = new ResizeObserver(() => setH(el.clientHeight))
    ro.observe(el)
    setH(el.clientHeight)
    return () => ro.disconnect()
  }, [ref])
  return h
}

/** The size an FLTK window was designed at (the engine reports it; older engines don't). */
function nativeSize(win: MWindow) {
  return { w: win.dw ?? win.w, h: win.dh ?? win.h }
}

/**
 * How a docked window fills a width x height panel: drawn at `scale`, with the real FLTK
 * window resized to w x h so FLTK re-lays it out (trees and lists grow, like resizing the
 * native window). Below the designed size the window keeps its designed layout and is
 * scaled down to fit instead.
 */
function fit(native: { w: number; h: number }, width: number, height: number) {
  const scale = Math.min(1, width / native.w, height / native.h)
  return { scale, w: Math.round(width / scale), h: Math.round(height / scale) }
}

/** A docked FLTK window that exactly fills its panel. */
function DockedWindow({ win, width, height }: { win: MWindow; width: number; height: number }) {
  const { scale, w, h } = fit(nativeSize(win), width, height)

  useEffect(() => {
    if (!height || !width || win.closing || (win.w === w && win.h === h)) return
    const t = window.setTimeout(() => sendMirror({ op: 'resize', win: win.id, w, h }), 60)
    return () => window.clearTimeout(t)
  }, [win.id, win.w, win.h, w, h, width, height, win.closing])

  return <MirrorWindow win={win} scale={scale} />
}

const widthKey = (side: string) => `webvsp-panel-${side}`

function loadWidth(side: string): number | null {
  try {
    const v = Number(localStorage.getItem(widthKey(side)))
    return v > 0 ? v : null
  } catch {
    return null
  }
}

function saveWidth(side: string, w: number | null) {
  try {
    if (w == null) localStorage.removeItem(widthKey(side))
    else localStorage.setItem(widthKey(side), String(Math.round(w)))
  } catch {
    // Not persisted: fine.
  }
}

/**
 * A side panel that slides open/closed, always spans the full height, and can be resized
 * by dragging its inner edge, down to the designed width (double-click the edge to reset).
 */
export function SidePanel({
  side,
  win,
  open,
  height,
}: {
  side: 'left' | 'right'
  win: MWindow | undefined
  open: boolean
  height: number
}) {
  // Remember the last window so the panel keeps its content while sliding closed.
  const last = useRef<MWindow | undefined>(win)
  if (win) last.current = win
  const shown = win ?? last.current
  const [custom, setCustom] = useState<number | null>(() => loadWidth(side))
  const [dragging, setDragging] = useState(false)
  const lastDown = useRef(0)

  const native = shown ? nativeSize(shown) : null
  // Default: the designed width (scaled down with the height if the panel is short).
  const auto = native ? Math.round(native.w * Math.min(1, height ? height / native.h : 1)) : 0
  // Narrower than designed would only shrink the text, so the designed width is the minimum.
  const minWidth = auto
  const maxWidth = Math.max(minWidth, Math.round(window.innerWidth * 0.45))
  const width = shown ? Math.min(maxWidth, Math.max(minWidth, custom ?? auto)) : 0
  const visible = open && !!win

  const onPointerDown = (e: React.PointerEvent<HTMLDivElement>) => {
    if (e.button !== 0) return
    e.preventDefault()
    // A second press in quick succession is a double-click: reset (dblclick alone isn't
    // reliable once the first press captured the pointer).
    if (e.timeStamp - lastDown.current < 350) {
      lastDown.current = 0
      reset()
      return
    }
    lastDown.current = e.timeStamp
    const el = e.currentTarget
    el.setPointerCapture(e.pointerId)
    const x0 = e.clientX
    const w0 = width
    let w = w0
    setDragging(true)
    const move = (ev: PointerEvent) => {
      const dx = ev.clientX - x0
      w = Math.min(maxWidth, Math.max(minWidth, w0 + (side === 'left' ? dx : -dx)))
      setCustom(w)
    }
    const up = () => {
      el.removeEventListener('pointermove', move)
      el.removeEventListener('pointerup', up)
      el.removeEventListener('pointercancel', up)
      setDragging(false)
      if (w !== w0) saveWidth(side, w)
    }
    el.addEventListener('pointermove', move)
    el.addEventListener('pointerup', up)
    el.addEventListener('pointercancel', up)
  }

  const reset = () => {
    setCustom(null)
    saveWidth(side, null)
  }

  return (
    <div
      className={`side-panel ${side}${dragging ? ' resizing' : ''}`}
      style={{ width: visible ? width : 0 }}
      aria-hidden={!visible}
    >
      <div className="side-panel-inner" style={{ width }}>
        {shown && height > 0 && (
          <div key={shown.id} className="dock-fade">
            <DockedWindow win={shown} width={width} height={height} />
          </div>
        )}
      </div>
      {visible && (
        <div
          className="side-panel-handle"
          role="separator"
          aria-orientation="vertical"
          title="Drag to resize · double-click to reset"
          onPointerDown={onPointerDown}
          onDoubleClick={reset}
        />
      )}
    </div>
  )
}

export function ViewerOverlay({ fullscreen, onToggle }: { fullscreen: boolean; onToggle: () => void }): ReactNode {
  return (
    <>
      <button
        className="viewer-fullscreen"
        title={fullscreen ? 'Show panels' : 'Hide panels (fullscreen view)'}
        onClick={onToggle}
      >
        {fullscreen ? (
          <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5">
            <path d="M6 1.5V6H1.5M10 1.5V6h4.5M6 14.5V10H1.5M10 14.5V10h4.5" />
          </svg>
        ) : (
          <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5">
            <path d="M1.5 6V1.5H6M14.5 6V1.5H10M1.5 10v4.5H6M14.5 10v4.5H10" />
          </svg>
        )}
      </button>
    </>
  )
}
