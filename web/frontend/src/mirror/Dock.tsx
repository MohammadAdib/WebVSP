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

// The height each FLTK window was designed at (first size seen), per screen.
const nativeHeights = new Map<string, number>()

/**
 * A docked FLTK window that always fills the panel height. When there's room, the real
 * window is resized so FLTK re-lays it out (trees and lists grow); when there isn't, it
 * goes back to its designed height and is scaled down to fit.
 */
function DockedWindow({ win, height }: { win: MWindow; height: number }) {
  const key = win.screen || win.title
  if (!nativeHeights.has(key)) nativeHeights.set(key, win.h)
  const native = nativeHeights.get(key)!
  const target = height >= native ? height : native
  const scale = height >= native ? 1 : height / native

  useEffect(() => {
    if (!height || win.closing || win.h === target) return
    const t = window.setTimeout(() => sendMirror({ op: 'resize', win: win.id, w: win.w, h: target }), 120)
    return () => window.clearTimeout(t)
  }, [win.id, win.w, win.h, target, height, win.closing])

  return <MirrorWindow win={win} scale={scale} />
}

/** A side panel that slides open/closed and always spans the full height. */
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
  const native = shown ? nativeHeights.get(shown.screen || shown.title) ?? shown.h : 0
  const scale = height && native > height ? height / native : 1
  const width = shown ? Math.round(shown.w * scale) : 0
  const visible = open && !!win
  return (
    <div className={`side-panel ${side}`} style={{ width: visible ? width : 0 }} aria-hidden={!visible}>
      <div className="side-panel-inner" style={{ width }}>
        {shown && height > 0 && (
          <div key={shown.id} className="dock-fade">
            <DockedWindow win={shown} height={height} />
          </div>
        )}
      </div>
    </div>
  )
}

export function ViewerOverlay({ fullscreen, onToggle }: { fullscreen: boolean; onToggle: () => void }): ReactNode {
  return (
    <>
      <img className="viewer-logo" src="/logo.png" alt="" draggable={false} />
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
