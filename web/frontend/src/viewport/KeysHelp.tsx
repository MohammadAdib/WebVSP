import { useEffect, useRef, useState } from 'react'

/** The 3D view's keys, Blender's, grouped as the popup lists them. */
const SECTIONS: { title: string; keys: [string, string][] }[] = [
  {
    title: 'Select and show',
    keys: [
      ['Click', 'Select a geom'],
      ['Shift / Ctrl + click', 'Add to or take from the selection'],
      ['A', 'Select all'],
      ['Alt + A', 'Deselect all'],
      ['H', 'Hide the selection'],
      ['Shift + H', 'Hide everything else'],
      ['Alt + H', 'Show everything'],
      ['Z', 'Selection: wireframe / shaded'],
      ['Shift + Z', 'Every geom: wireframe / shaded'],
    ],
  },
  {
    title: 'Move and rotate',
    keys: [
      ['G', 'Move the selection with the mouse'],
      ['R', 'Rotate, each geom about its own origin'],
      ['Shift + D', 'Duplicate, then move the copies'],
      ['X / Del', 'Delete the selection (asks first)'],
      ['X / Y / Z', 'Hold to that axis'],
      ['G  X  10', 'Move 10 along X (type the value)'],
      ['R  Z  90', 'Rotate 90° about Z'],
      ['Ctrl', 'Snap while moving the mouse'],
      ['Enter / click', 'Apply'],
      ['Esc / right-click', 'Cancel'],
    ],
  },
  {
    title: 'View',
    keys: [
      ['1 / 3 / 7', 'Front / side / top (Ctrl: the other way)'],
      ['5', 'Perspective / orthographic'],
      ['2 4 6 8', 'Orbit in 30° steps'],
      ['9', 'Look from the other side'],
      ['F / Home / .', 'Fit the model'],
      ['C', 'Center the model (zoom kept)'],
      ['Left drag', 'Orbit'],
      ['Right drag', 'Pan'],
      ['Wheel', 'Zoom to the cursor'],
    ],
  },
]

function KeyboardIcon() {
  return (
    <svg width="18" height="18" viewBox="0 0 18 18" fill="none" stroke="currentColor" strokeWidth="1.3" strokeLinejoin="round" aria-hidden="true">
      <rect x="1.5" y="4.5" width="15" height="9.5" rx="1.5" />
      <path d="M4.5 7.5h1M7.5 7.5h1M10.5 7.5h1M13 7.5h.5M4.5 10h1M13 10h.5M7 11.2h4" strokeLinecap="round" />
    </svg>
  )
}

/** A keyboard button, floating beside the view toolbar, that shows the 3D view's keys. */
export function KeysHelp() {
  const [open, setOpen] = useState(false)
  const ref = useRef<HTMLSpanElement>(null)
  useEffect(() => {
    if (!open) return
    const onDown = (e: PointerEvent) => {
      if (!ref.current?.contains(e.target as Node)) setOpen(false)
    }
    const onKey = (e: KeyboardEvent) => e.key === 'Escape' && setOpen(false)
    window.addEventListener('pointerdown', onDown)
    window.addEventListener('keydown', onKey)
    return () => {
      window.removeEventListener('pointerdown', onDown)
      window.removeEventListener('keydown', onKey)
    }
  }, [open])
  return (
    <span className="keys-help" ref={ref}>
      <button
        className={open ? 'on' : undefined}
        onClick={() => setOpen(!open)}
        title="Keyboard shortcuts"
        aria-label="Keyboard shortcuts"
        aria-expanded={open}
      >
        <KeyboardIcon />
      </button>
      {open && (
        <div className="keys-popup" role="dialog" aria-label="Keyboard shortcuts">
          {SECTIONS.map((s) => (
            <section key={s.title}>
              <h4>{s.title}</h4>
              <dl>
                {s.keys.map(([k, what]) => (
                  <div key={k}>
                    <dt>
                      {/* Keys as keycaps; ' / ', ' + ' and a double space (a sequence) between them. */}
                      {k.split(/(\s\/\s|\s\+\s|\s{2})/).map((part, i) =>
                        i % 2 ? <span key={i}>{part.trim() || ' '}</span> : <kbd key={i}>{part}</kbd>,
                      )}
                    </dt>
                    <dd>{what}</dd>
                  </div>
                ))}
              </dl>
            </section>
          ))}
        </div>
      )}
    </span>
  )
}
