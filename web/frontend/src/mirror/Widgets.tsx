import { createContext, useContext, useEffect, useRef, useState, type CSSProperties, type ReactNode } from 'react'
import { ALIGN, BUTTON_TYPE, boxStyle, browserCell, isGray, labelText, modsOf, parseSymbol } from './fltk'
import { GlBody, ImageBody } from './Custom'
import { childKey, sendMirror, useMirror } from './store'
import type { MenuItem, MWidget, TreeItem } from './types'

/** Window being rendered: its id, and a function mapping client coords to FLTK window coords. */
export interface WinCtx {
  win: number
  toWin: (clientX: number, clientY: number) => [number, number]
}
export const WindowContext = createContext<WinCtx>({ win: 0, toWin: () => [0, 0] })

const NO_CHILDREN: number[] = []

function useChildren(win: number, parent: number) {
  return useMirror((s) => s.children[childKey(win, parent)] ?? NO_CHILDREN)
}

export function Children({ win, parent, px, py }: { win: number; parent: number; px: number; py: number }) {
  const ids = useChildren(win, parent)
  return (
    <>
      {ids.map((id) => (
        <WidgetView key={id} id={id} px={px} py={py} />
      ))}
    </>
  )
}

/** Sends values with at most one request in flight per widget, keeping only the newest. */
function useThrottledSet(id: number) {
  const pending = useRef<string | null>(null)
  const timer = useRef<number | null>(null)
  return (v: string) => {
    pending.current = v
    if (timer.current !== null) return
    sendMirror({ op: 'set', id, v })
    pending.current = null
    timer.current = window.setTimeout(function flush() {
      timer.current = null
      if (pending.current !== null) {
        const p = pending.current
        pending.current = null
        sendMirror({ op: 'set', id, v: p })
        timer.current = window.setTimeout(flush, 40)
      }
    }, 40)
  }
}

function fontStyle(w: MWidget): CSSProperties {
  return {
    fontSize: Math.max(9, w.ls || 12),
    fontWeight: w.lf % 2 === 1 ? 600 : 400,
    fontStyle: w.lf % 4 >= 2 ? 'italic' : undefined,
  }
}

function tipOf(w: MWidget) {
  const parts = [w.tip, w.desc, w.parm].filter(Boolean)
  return parts.length ? parts.join('\n') : undefined
}

/** Label drawn outside a widget per its FLTK align bits (inputs, sliders, choices, groups). */
function OutsideLabel({ w }: { w: MWidget }) {
  const text = labelText(w.label)
  if (!text) return null
  const a = w.align
  const fs = fontStyle(w)
  const style: CSSProperties = { position: 'absolute', whiteSpace: 'nowrap', color: 'var(--fg-1)', ...fs, lineHeight: '1.2' }
  if (a & ALIGN.TOP) {
    Object.assign(style, { bottom: '100%', left: a & ALIGN.LEFT ? 0 : undefined, right: a & ALIGN.RIGHT ? 0 : undefined })
    if (!(a & (ALIGN.LEFT | ALIGN.RIGHT))) Object.assign(style, { left: '50%', transform: 'translateX(-50%)' })
  } else if (a & ALIGN.BOTTOM) {
    Object.assign(style, { top: '100%', left: a & ALIGN.LEFT ? 0 : undefined, right: a & ALIGN.RIGHT ? 0 : undefined })
    if (!(a & (ALIGN.LEFT | ALIGN.RIGHT))) Object.assign(style, { left: '50%', transform: 'translateX(-50%)' })
  } else if (a & ALIGN.RIGHT) {
    Object.assign(style, { left: '100%', top: '50%', transform: 'translateY(-50%)', paddingLeft: 3 })
  } else {
    Object.assign(style, { right: '100%', top: '50%', transform: 'translateY(-50%)', paddingRight: 3 })
  }
  return <span style={style}>{text}</span>
}

function insideLabel(w: MWidget) {
  return (w.align & ALIGN.INSIDE) !== 0 || (w.align & 15) === 0
}

/** A label's content: text, or an FLTK symbol drawn rotated/flipped like FLTK does. */
function LabelContent({ label }: { label?: string }) {
  const sym = parseSymbol(label)
  if (!sym) return <>{labelText(label)}</>
  const scale = `${sym.flipX ? -1 : 1}, ${sym.flipY ? -1 : 1}`
  return (
    <span className="m-sym" style={{ transform: `rotate(${sym.rotate}deg) scale(${scale})` }}>
      {sym.glyph}
    </span>
  )
}

/** Inline label for widgets that draw their label inside (buttons, boxes). */
function InnerLabel({ w, children }: { w: MWidget; children?: ReactNode }) {
  const a = w.align
  const justify = a & ALIGN.LEFT ? 'flex-start' : a & ALIGN.RIGHT ? 'flex-end' : 'center'
  const alignItems = a & ALIGN.TOP ? 'flex-start' : a & ALIGN.BOTTOM ? 'flex-end' : 'center'
  return (
    <span
      className="m-inner"
      style={{ justifyContent: justify, alignItems, whiteSpace: a & ALIGN.WRAP ? 'normal' : 'nowrap', ...fontStyle(w) }}
    >
      {children ?? <LabelContent label={w.label} />}
    </span>
  )
}

export function WidgetView({ id, px, py }: { id: number; px: number; py: number }) {
  const w = useMirror((s) => s.widgets[id])
  if (!w || !w.vis) return null
  const style: CSSProperties = { left: w.x - px, top: w.y - py, width: w.w, height: w.h }
  const cls = 'm-w m-' + w.k + (w.act ? '' : ' m-inactive')
  return (
    <div className={cls} style={style} title={tipOf(w)} data-id={w.id}>
      <Body w={w} />
    </div>
  )
}

function Body({ w }: { w: MWidget }) {
  switch (w.k) {
    case 'group':
    case 'subwindow':
      return <GroupBody w={w} />
    case 'tabs':
      return <TabsBody w={w} />
    case 'scroll':
      return <ScrollBody w={w} />
    case 'box':
      return <BoxBody w={w} />
    case 'button':
    case 'repeat':
      return <ButtonBody w={w} />
    case 'light':
      return <LightBody w={w} />
    case 'check':
    case 'round':
      return <CheckBody w={w} />
    case 'input':
    case 'output':
      return <InputBody w={w} />
    case 'slider':
    case 'value_slider':
    case 'scrollbar':
      return <SliderBody w={w} />
    case 'counter':
      return <CounterBody w={w} />
    case 'choice':
      return <ChoiceBody w={w} />
    case 'menubutton':
      return <MenuButtonBody w={w} />
    case 'input_choice':
      return <InputChoiceBody w={w} />
    case 'browser':
    case 'check_browser':
      return <BrowserBody w={w} />
    case 'tree':
      return <TreeBody w={w} />
    case 'text_display':
    case 'text_editor':
    case 'terminal':
      return <TextBody w={w} />
    case 'progress':
      return <ProgressBody w={w} />
    case 'menubar':
      return null // drawn by the app's top bar
    case 'gl':
      return <GlBody w={w} />
    case 'canvas':
    case 'table':
      return <ImageBody w={w} />
    default:
      return <GroupBody w={w} />
  }
}

function BoxFill({ w }: { w: MWidget }) {
  const kind = boxStyle(w.box)
  if (kind === 'none') return null
  // Unlabelled colored boxes show data (a geom's color, a swatch); labelled ones are
  // section headers and take the gray theme.
  const swatch = !w.label && !isGray(w.col)
  const header = !!w.label && !isGray(w.col)
  return (
    <div
      className={'m-box m-box-' + kind + (header ? ' m-box-header' : '')}
      style={swatch ? { background: w.col, borderColor: 'transparent' } : undefined}
    />
  )
}

function GroupBody({ w }: { w: MWidget }) {
  const parentIsTabs = useMirror((s) => s.widgets[w.p]?.k === 'tabs')
  return (
    <>
      <BoxFill w={w} />
      {!parentIsTabs && w.label && !insideLabel(w) && <OutsideLabel w={w} />}
      <Children win={w.win} parent={w.id} px={w.x} py={w.y} />
    </>
  )
}

function ScrollBody({ w }: { w: MWidget }) {
  const { win, toWin } = useContext(WindowContext)
  return (
    <div
      className="m-clip"
      onWheel={(e) => {
        const [x, y] = toWin(e.clientX, e.clientY)
        sendMirror({ op: 'mouse', win, kind: 'wheel', x, y, dy: Math.sign(e.deltaY) * 3 })
      }}
    >
      <BoxFill w={w} />
      <Children win={w.win} parent={w.id} px={w.x} py={w.y} />
    </div>
  )
}

function BoxBody({ w }: { w: MWidget }) {
  return (
    <>
      <BoxFill w={w} />
      {w.label && (insideLabel(w) ? <InnerLabel w={w} /> : <OutsideLabel w={w} />)}
    </>
  )
}

function TabsBody({ w }: { w: MWidget }) {
  const ids = useChildren(w.win, w.id)
  const widgets = useMirror((s) => s.widgets)
  const kids = ids.map((id) => widgets[id]).filter(Boolean)
  const headerH = kids.length ? Math.max(18, Math.min(...kids.map((k) => k.y)) - w.y) : 24
  return (
    <>
      <div className="m-tabbar" style={{ height: headerH }}>
        {kids.map((k) => (
          <button
            key={k.id}
            className={'m-tab' + (k.id === w.v ? ' on' : '')}
            style={{ fontWeight: fontStyle(k).fontWeight }}
            onClick={() => sendMirror({ op: 'tab', id: w.id, child: k.id })}
          >
            {labelText(k.label)}
          </button>
        ))}
      </div>
      <div className="m-tabpage" style={{ top: headerH }} />
      <Children win={w.win} parent={w.id} px={w.x} py={w.y} />
    </>
  )
}

function ButtonBody({ w }: { w: MWidget }) {
  const repeatTimer = useRef<number | null>(null)
  const stopRepeat = () => {
    if (repeatTimer.current !== null) window.clearTimeout(repeatTimer.current)
    repeatTimer.current = null
  }
  useEffect(() => stopRepeat, [])
  const pressed = w.type !== BUTTON_TYPE.NORMAL && w.v === 1
  const swatch = !w.label && !isGray(w.col)
  const frameless = boxStyle(w.box) === 'none'
  const cls = 'm-btn' + (pressed ? ' on' : '') + (frameless ? ' bare' : '')
  if (w.k === 'repeat') {
    return (
      <button
        className={cls}
        onPointerDown={() => {
          sendMirror({ op: 'click', id: w.id })
          const tick = (delay: number) => {
            repeatTimer.current = window.setTimeout(() => {
              sendMirror({ op: 'click', id: w.id })
              tick(80)
            }, delay)
          }
          tick(400)
        }}
        onPointerUp={stopRepeat}
        onPointerLeave={stopRepeat}
      >
        <InnerLabel w={w} />
      </button>
    )
  }
  return (
    <button className={cls} style={swatch ? { background: w.col } : undefined} onClick={() => sendMirror({ op: 'click', id: w.id })}>
      {w.label && insideLabel(w) && <InnerLabel w={w} />}
      {w.label && !insideLabel(w) && <OutsideLabel w={w} />}
    </button>
  )
}

function LightBody({ w }: { w: MWidget }) {
  return (
    <button className={'m-btn m-light-btn' + (w.v ? ' on' : '')} onClick={() => sendMirror({ op: 'click', id: w.id })}>
      <span className={'m-led' + (w.v ? ' on' : '')} />
      <InnerLabel w={{ ...w, align: (w.align & ~15) | ALIGN.LEFT | ALIGN.INSIDE }} />
    </button>
  )
}

function CheckBody({ w }: { w: MWidget }) {
  return (
    <label className="m-check" onClick={(e) => (e.preventDefault(), sendMirror({ op: 'click', id: w.id }))}>
      <span className={(w.k === 'round' ? 'm-radio' : 'm-checkbox') + (w.v ? ' on' : '')} />
      <span style={fontStyle(w)}>{labelText(w.label)}</span>
    </label>
  )
}

function InputBody({ w }: { w: MWidget }) {
  const [text, setText] = useState<string | null>(null)
  const value = String(w.v ?? '')
  const readOnly = w.k === 'output' || w.ro
  const commit = () => {
    if (text !== null && text !== value) sendMirror({ op: 'set', id: w.id, v: text })
    setText(null)
  }
  return (
    <>
      <input
        className="m-input"
        value={text ?? value}
        readOnly={readOnly}
        style={{ fontSize: w.ts || 12 }}
        onChange={(e) => setText(e.target.value)}
        onBlur={commit}
        onKeyDown={(e) => {
          if (e.key === 'Enter') {
            commit()
            e.currentTarget.blur()
          }
          if (e.key === 'Escape') {
            setText(null)
            e.currentTarget.blur()
          }
          e.stopPropagation()
        }}
      />
      {w.label && <OutsideLabel w={w} />}
    </>
  )
}

function SliderBody({ w }: { w: MWidget }) {
  const set = useThrottledSet(w.id)
  const [drag, setDrag] = useState<number | null>(null)
  const ref = useRef<HTMLDivElement>(null)
  const min = w.min ?? 0
  const max = w.max ?? 1
  const v = drag ?? Number(w.v ?? 0)
  const horizontal = w.k === 'scrollbar' ? (w.type ?? 0) % 2 === 1 : (w.type ?? 1) % 2 === 1
  const span = max - min || 1
  const frac = Math.min(1, Math.max(0, (v - min) / span))
  const thumbFrac = w.k === 'scrollbar' ? Math.max(0.06, Math.min(1, w.ss ?? 0.1)) : 0
  const valueText = w.k === 'value_slider' ? Number(v.toPrecision(6)).toString() : null

  const fromPointer = (clientX: number, clientY: number) => {
    const r = ref.current!.getBoundingClientRect()
    const t = horizontal ? (clientX - r.left) / r.width : (clientY - r.top) / r.height
    const f = thumbFrac ? (t - thumbFrac / 2) / (1 - thumbFrac) : t
    let nv = min + Math.min(1, Math.max(0, f)) * span
    const step = w.step ?? 0
    if (step > 0) nv = min + Math.round((nv - min) / step) * step
    return nv
  }

  const pos = thumbFrac ? frac * (1 - thumbFrac) : frac
  return (
    <>
      <div
        ref={ref}
        className={'m-slider' + (horizontal ? ' h' : ' v') + (w.k === 'scrollbar' ? ' sb' : '')}
        style={valueText !== null ? (horizontal ? { left: 44 } : { top: 20 }) : undefined}
        onPointerDown={(e) => {
          e.currentTarget.setPointerCapture(e.pointerId)
          const nv = fromPointer(e.clientX, e.clientY)
          setDrag(nv)
          set(String(nv))
        }}
        onPointerMove={(e) => {
          if (drag === null) return
          const nv = fromPointer(e.clientX, e.clientY)
          setDrag(nv)
          set(String(nv))
        }}
        onPointerUp={() => setDrag(null)}
        onWheel={(e) => {
          if (w.k !== 'scrollbar') return
          const step = (w.step || 1) * 3 * Math.sign(e.deltaY)
          set(String(Math.min(Math.max(v + step, Math.min(min, max)), Math.max(min, max))))
        }}
      >
        <div className="m-track" />
        {horizontal ? (
          <div className="m-thumb" style={thumbFrac ? { left: `${pos * 100}%`, width: `${thumbFrac * 100}%` } : { left: `calc(${pos * 100}% - 4px)` }} />
        ) : (
          <div className="m-thumb" style={thumbFrac ? { top: `${pos * 100}%`, height: `${thumbFrac * 100}%` } : { top: `calc(${pos * 100}% - 4px)` }} />
        )}
      </div>
      {valueText !== null && <span className="m-vs-text">{valueText}</span>}
      {w.label && <OutsideLabel w={w} />}
    </>
  )
}

function CounterBody({ w }: { w: MWidget }) {
  const v = Number(w.v ?? 0)
  const step = w.step || 1
  const lo = Math.min(w.min ?? -Infinity, w.max ?? Infinity)
  const hi = Math.max(w.min ?? -Infinity, w.max ?? Infinity)
  const bounded = (w.min ?? 0) !== (w.max ?? 0)
  const set = (nv: number) => sendMirror({ op: 'set', id: w.id, v: String(bounded ? Math.min(hi, Math.max(lo, nv)) : nv) })
  const simple = w.type === 1
  return (
    <>
      <div className="m-counter">
        {!simple && <button onClick={() => set(v - step * 10)}>«</button>}
        <button onClick={() => set(v - step)}>‹</button>
        <span>{Number(v.toPrecision(6))}</span>
        <button onClick={() => set(v + step)}>›</button>
        {!simple && <button onClick={() => set(v + step * 10)}>»</button>}
      </div>
      {w.label && <OutsideLabel w={w} />}
    </>
  )
}

function visibleItems(w: MWidget) {
  return ((w.items ?? []) as MenuItem[]).filter((i) => i.d === 0 && !(i.flags & 0x10))
}

function ChoiceBody({ w }: { w: MWidget }) {
  const items = visibleItems(w)
  return (
    <>
      <select
        className="m-select"
        value={String(w.v ?? -1)}
        style={{ fontSize: w.ts || 12 }}
        onChange={(e) => sendMirror({ op: 'pick', id: w.id, i: Number(e.target.value) })}
      >
        {w.v === -1 && <option value="-1" />}
        {items.map((it) => (
          <option key={it.i} value={it.i} disabled={(it.flags & 1) !== 0}>
            {labelText(it.label)}
          </option>
        ))}
      </select>
      {w.label && <OutsideLabel w={w} />}
    </>
  )
}

function MenuButtonBody({ w }: { w: MWidget }) {
  const [open, setOpen] = useState(false)
  const items = visibleItems(w)
  return (
    <>
      <button className="m-btn" onClick={() => setOpen(!open)}>
        <InnerLabel w={w}>
          {labelText(w.label)} <span style={{ marginLeft: 4, opacity: 0.6 }}>▾</span>
        </InnerLabel>
      </button>
      {open && (
        <div className="m-popup" onMouseLeave={() => setOpen(false)}>
          {items.map((it) => (
            <button key={it.i} onClick={() => (setOpen(false), sendMirror({ op: 'pick', id: w.id, i: it.i }))}>
              {labelText(it.label)}
            </button>
          ))}
        </div>
      )}
    </>
  )
}

function InputChoiceBody({ w }: { w: MWidget }) {
  const items = visibleItems(w)
  const [text, setText] = useState<string | null>(null)
  const commit = () => {
    if (text !== null) sendMirror({ op: 'set', id: w.id, v: text })
    setText(null)
  }
  return (
    <>
      <div className="m-inputchoice">
        <input
          className="m-input"
          value={text ?? String(w.v ?? '')}
          onChange={(e) => setText(e.target.value)}
          onBlur={commit}
          onKeyDown={(e) => {
            if (e.key === 'Enter') commit()
            e.stopPropagation()
          }}
        />
        <select value="" onChange={(e) => sendMirror({ op: 'pick', id: w.id, i: Number(e.target.value) })}>
          <option value="" />
          {items.map((it) => (
            <option key={it.i} value={it.i}>
              {labelText(it.label)}
            </option>
          ))}
        </select>
      </div>
      {w.label && <OutsideLabel w={w} />}
    </>
  )
}

function BrowserBody({ w }: { w: MWidget }) {
  const lines = w.lines ?? []
  const check = w.k === 'check_browser'
  const sep = w.csep || '\t'
  const cols = w.cols ?? []
  return (
    <>
      <div className="m-browser" style={{ fontSize: w.ts || 12 }}>
        {lines.map((line, i) => {
          if (line.v === false) return null
          const cells = line.t.split(sep)
          return (
            <div
              key={i}
              className={'m-bline' + (line.s ? ' sel' : '')}
              onClick={(e) => !check && sendMirror({ op: 'browser', id: w.id, line: i + 1, clicks: 1, mods: modsOf(e) })}
              onDoubleClick={(e) => !check && sendMirror({ op: 'browser', id: w.id, line: i + 1, clicks: 2, mods: modsOf(e) })}
            >
              {check && (
                <span
                  className={'m-checkbox' + (line.c ? ' on' : '')}
                  onClick={() => sendMirror({ op: 'check', id: w.id, line: i + 1, v: line.c ? 0 : 1 })}
                />
              )}
              {cells.map((c, ci) => {
                const cell = browserCell(c)
                const width = ci < cols.length ? cols[ci] : undefined
                return (
                  <span
                    key={ci}
                    className="m-bcell"
                    style={{
                      width,
                      flex: width ? 'none' : 1,
                      fontWeight: cell.bold ? 600 : undefined,
                      fontStyle: cell.italic ? 'italic' : undefined,
                      textAlign: cell.center ? 'center' : cell.right ? 'right' : undefined,
                    }}
                  >
                    {cell.text}
                  </span>
                )
              })}
            </div>
          )
        })}
      </div>
      {w.label && !insideLabel(w) && <OutsideLabel w={w} />}
    </>
  )
}

function TreeBody({ w }: { w: MWidget }) {
  const { win, toWin } = useContext(WindowContext)
  const items = (w.items ?? []) as TreeItem[]
  const down = useRef(0)
  const mouse = (kind: string, e: React.PointerEvent | React.MouseEvent, clicks = 0) => {
    const [x, y] = toWin(e.clientX, e.clientY)
    sendMirror({ op: 'mouse', win, kind, x, y, b: 1, mods: modsOf(e), clicks })
  }
  return (
    <div
      className="m-tree"
      onPointerDown={(e) => {
        if ((e.target as HTMLElement).dataset.icon) return
        const now = performance.now()
        const clicks = now - down.current < 350 ? 1 : 0
        down.current = now
        mouse('push', e, clicks)
        mouse('release', e, clicks)
      }}
      onWheel={(e) => {
        const [x, y] = toWin(e.clientX, e.clientY)
        sendMirror({ op: 'mouse', win, kind: 'wheel', x, y, dy: Math.sign(e.deltaY) * 3 })
      }}
    >
      {items.map((it) => (
        <div
          key={it.n}
          className={'m-titem' + (it.sel ? ' sel' : '')}
          style={{ left: it.x - w.x, top: it.y - w.y, width: Math.max(0, w.w - (it.x - w.x) - 16), height: it.h, opacity: it.act ? 1 : 0.5 }}
        >
          {it.kids > 0 && (
            <span className="m-twist" style={{ left: Math.max(0, it.lx - it.x - 26) }}>
              {it.open ? '−' : '+'}
            </span>
          )}
          {it.showxy && (
            <span
              data-icon="1"
              className={'m-ticon' + (it.show ? '' : ' off')}
              style={{ left: it.showxy[0] - it.x, width: it.showxy[2] || 14 }}
              onPointerDown={(e) => {
                e.stopPropagation()
                sendMirror({ op: 'tree_icon', id: w.id, item: it.n, icon: 1 })
              }}
            >
              ◉
            </span>
          )}
          {it.surfxy && (
            <span
              data-icon="1"
              className="m-ticon"
              style={{ left: it.surfxy[0] - it.x, width: it.surfxy[2] || 14 }}
              onPointerDown={(e) => {
                e.stopPropagation()
                sendMirror({ op: 'tree_icon', id: w.id, item: it.n, icon: 2 })
              }}
            >
              {['▦', '▢', '◧', '■', '·'][it.surf ?? 0] ?? '▦'}
            </span>
          )}
          <span
            className="m-tlabel"
            style={{ left: it.lx - it.x, maxWidth: it.showxy ? Math.max(0, it.showxy[0] - it.lx - 2) : undefined }}
          >
            {it.label}
          </span>
        </div>
      ))}
    </div>
  )
}

function TextBody({ w }: { w: MWidget }) {
  const editable = w.k === 'text_editor'
  const [text, setText] = useState<string | null>(null)
  const ref = useRef<HTMLTextAreaElement>(null)
  useEffect(() => {
    // Keep logs scrolled to the end, like the FLTK consoles.
    if (!editable && ref.current) ref.current.scrollTop = ref.current.scrollHeight
  }, [w.v, editable])
  return (
    <textarea
      ref={ref}
      className="m-text"
      readOnly={!editable}
      value={text ?? String(w.v ?? '')}
      style={{ fontSize: w.ts || 12 }}
      onChange={(e) => setText(e.target.value)}
      onBlur={() => {
        if (text !== null) sendMirror({ op: 'text', id: w.id, v: text })
        setText(null)
      }}
      onKeyDown={(e) => e.stopPropagation()}
    />
  )
}

function ProgressBody({ w }: { w: MWidget }) {
  const min = w.min ?? 0
  const max = w.max ?? 100
  const frac = max > min ? (Number(w.v ?? 0) - min) / (max - min) : 0
  return (
    <div className="m-progress">
      <div style={{ width: `${Math.max(0, Math.min(1, frac)) * 100}%` }} />
      <InnerLabel w={w} />
    </div>
  )
}
