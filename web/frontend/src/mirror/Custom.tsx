import { useContext, useEffect, useMemo, useRef } from 'react'
import { modsOf } from './fltk'
import { sendMirror } from './store'
import type { GlDraw, MWidget } from './types'
import { WindowContext } from './Widgets'

/** Forward pointer/wheel input on a custom-drawn widget to the real FLTK widget. */
function useRawInput() {
  const { win, toWin } = useContext(WindowContext)
  const buttons = useRef(0)
  const clicks = useRef({ t: 0 })
  const send = (kind: string, e: React.PointerEvent | React.WheelEvent, extra: Record<string, number> = {}) => {
    const [x, y] = toWin(e.clientX, e.clientY)
    sendMirror({ op: 'mouse', win, kind, x, y, mods: modsOf(e), ...extra })
  }
  return {
    onPointerDown: (e: React.PointerEvent) => {
      e.currentTarget.setPointerCapture(e.pointerId)
      const now = performance.now()
      const dbl = now - clicks.current.t < 350 ? 1 : 0
      clicks.current.t = now
      buttons.current = e.button === 2 ? 3 : e.button === 1 ? 2 : 1
      send('push', e, { b: buttons.current, clicks: dbl })
    },
    onPointerMove: (e: React.PointerEvent) => {
      if (buttons.current) send('drag', e, { b: buttons.current })
    },
    onPointerUp: (e: React.PointerEvent) => {
      if (!buttons.current) return
      send('release', e, { b: buttons.current })
      buttons.current = 0
    },
    onWheel: (e: React.WheelEvent) => send('wheel', e, { dy: Math.sign(e.deltaY) }),
    onContextMenu: (e: React.MouseEvent) => e.preventDefault(),
  }
}

// Line colors are chosen for VSP's light GL background; lift dark ones for our dark view.
function onDark(hex: string) {
  const r = parseInt(hex.slice(1, 3), 16)
  const g = parseInt(hex.slice(3, 5), 16)
  const b = parseInt(hex.slice(5, 7), 16)
  const lum = 0.299 * r + 0.587 * g + 0.114 * b
  if (lum > 90) return hex
  if (Math.max(r, g, b) - Math.min(r, g, b) < 30) return '#d0d0d0'
  // Keep the hue, raise the lightness.
  const k = 150 / Math.max(1, Math.max(r, g, b))
  const c = (v: number) => Math.min(255, Math.round(v * k + 60)).toString(16).padStart(2, '0')
  return `#${c(r)}${c(g)}${c(b)}`
}

/** A VSP GL view (XSec preview, curve editor, ...) drawn from its DrawObjs with VSP's camera math. */
export function GlBody({ w }: { w: MWidget }) {
  const input = useRawInput()
  const draw = w.draw as GlDraw | undefined
  const shapes = useMemo(() => {
    if (!draw) return []
    const ppu = draw.ppu || 1
    const pw = w.w * ppu
    const ph = w.h * ppu
    const zoom = draw.zoom || 1
    // Inverse of EditXSecWindow::PixelToCoord: data = zoom * (pix - size/2) - pan  (GL y is up).
    const toPix = (d1: number, d2: number): [number, number] => {
      const x = ((d1 + draw.px) / zoom + pw / 2) / ppu
      const yUp = ((d2 + draw.py) / zoom + ph / 2) / ppu
      return [x, w.h - yUp]
    }
    const pick = (p: number[], i: number): [number, number] =>
      draw.plane === 1 ? [p[i], p[i + 2]] : draw.plane === 2 ? [p[i + 2], p[i + 1]] : [p[i], p[i + 1]]
    return draw.objs.map((o) => {
      const pts: [number, number][] = []
      for (let i = 0; i + 2 < o.p.length; i += 3) pts.push(toPix(...pick(o.p, i)))
      return { o, pts }
    })
  }, [draw, w.w, w.h])

  return (
    <svg className="m-gl" width={w.w} height={w.h} {...input}>
      {shapes.map(({ o, pts }, k) => {
        const stroke = onDark(o.c)
        const lw = Math.max(1, o.lw || 1)
        if (o.t === 'points') {
          const r = Math.max(2, (o.ps || 4) / 2)
          const fill = onDark(o.pc)
          return (
            <g key={k}>
              {pts.map(([x, y], i) => (
                <circle key={i} cx={x} cy={y} r={r} fill={fill} />
              ))}
            </g>
          )
        }
        if (o.t === 'lines') {
          let d = ''
          for (let i = 0; i + 1 < pts.length; i += 2) d += `M${pts[i][0]},${pts[i][1]}L${pts[i + 1][0]},${pts[i + 1][1]}`
          return <path key={k} d={d} stroke={stroke} strokeWidth={lw} fill="none" />
        }
        const d = pts.map(([x, y], i) => `${i ? 'L' : 'M'}${x},${y}`).join('') + (o.t === 'loop' ? 'Z' : '')
        return <path key={k} d={d} stroke={stroke} strokeWidth={lw} fill="none" strokeLinejoin="round" />
      })}
    </svg>
  )
}

/** Plots and tables: pixels rendered by the real widget, inverted to suit the dark theme. */
export function ImageBody({ w }: { w: MWidget }) {
  const input = useRawInput()
  const ref = useRef<HTMLCanvasElement>(null)
  useEffect(() => {
    const c = ref.current
    if (!c || !w.img || !w.iw || !w.ih) return
    const bytes = Uint8Array.from(atob(w.img), (ch) => ch.charCodeAt(0))
    const img = new ImageData(w.iw, w.ih)
    for (let i = 0, j = 0; i < bytes.length; i += 3, j += 4) {
      img.data[j] = bytes[i]
      img.data[j + 1] = bytes[i + 1]
      img.data[j + 2] = bytes[i + 2]
      img.data[j + 3] = 255
    }
    c.width = w.iw
    c.height = w.ih
    c.getContext('2d')!.putImageData(img, 0, 0)
  }, [w.img, w.iw, w.ih])
  if (!w.img) return <div className="m-placeholder" />
  return <canvas ref={ref} className="m-image" style={{ width: w.w, height: w.h }} {...input} />
}
