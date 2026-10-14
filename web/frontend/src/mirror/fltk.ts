// FLTK conventions needed to draw mirrored widgets.

export const ALIGN = { TOP: 1, BOTTOM: 2, LEFT: 4, RIGHT: 8, INSIDE: 16, WRAP: 128 }

export const MENU = { INACTIVE: 1, TOGGLE: 2, VALUE: 4, RADIO: 8, INVISIBLE: 0x10, SUBMENU: 0x40, DIVIDER: 0x80 }

export const KEY = { SHIFT: 0x10000, CTRL: 0x40000, ALT: 0x80000, F: 0xffbd }

export const BUTTON_TYPE = { NORMAL: 0, TOGGLE: 1, RADIO: 102 }

// FLTK box types that draw only a frame (no fill).
const FRAMES = new Set([4, 5, 8, 9, 12, 13, 16])

export function boxStyle(box: number): 'none' | 'frame' | 'fill' {
  if (box === 0) return 'none'
  if (FRAMES.has(box)) return 'frame'
  return 'fill'
}

// FLTK symbol labels ("@<", "@2>>", "@-2->"...). Glyphs are for the symbol pointing right
// (keypad direction 6); a direction digit rotates it (see parseSymbol).
const SYMBOLS: Record<string, string> = {
  '>': '\u25B6', '<': '\u25C0', '>>': '\u25B6\u25B6', '<<': '\u25C0\u25C0', '->': '\u2192', '<-': '\u2190',
  '|>': '\u25B6|', '<|': '|\u25C0', '>|': '\u25B6|', '|<': '|\u25C0', '||': '\u2016', '[]': '\u25A0',
  'circle': '\u25CF', 'square': '\u25A0', 'line': '\u2014', 'menu': '\u2630', 'search': '\u2315',
  '+': '+', 'UpArrow': '\u25B2', 'DnArrow': '\u25BC', 'reload': '\u21BB', 'undo': '\u21B6', 'redo': '\u21B7',
  'returnarrow': '\u21B5', 'arrow': '\u2192', 'filenew': '\u2610', 'fileopen': '\u25A1', 'filesave': '\u25A3',
}

// Keypad direction digit -> counter-clockwise rotation in degrees (6 = no rotation).
const DIRECTION: Record<string, number> = { '6': 0, '9': 45, '8': 90, '7': 135, '4': 180, '1': 225, '2': 270, '3': 315, '5': 0 }

export interface FltkSymbol {
  glyph: string
  /** clockwise degrees, for CSS rotate() */
  rotate: number
  flipX: boolean
  flipY: boolean
}

/**
 * Parse an FLTK symbol label: "@" [#] [+|-digit size] [direction digit | 0nnn angle] [$ %] name.
 * Returns null for ordinary text labels.
 */
export function parseSymbol(raw: string | undefined): FltkSymbol | null {
  if (!raw || !raw.startsWith('@') || raw.startsWith('@@')) return null
  let s = raw.slice(1)
  if (s.startsWith('#')) s = s.slice(1)
  if (/^[+-]\d/.test(s)) s = s.slice(2)
  let ccw = 0
  const angle = /^0(\d{3})/.exec(s)
  if (angle) {
    ccw = Number(angle[1])
    s = s.slice(4)
  } else if (/^\d(?=\D)/.test(s)) {
    ccw = DIRECTION[s[0]] ?? 0
    s = s.slice(1)
  }
  let flipX = false
  let flipY = false
  while (s[0] === '$' || s[0] === '%') {
    if (s[0] === '$') flipX = true
    else flipY = true
    s = s.slice(1)
  }
  const glyph = SYMBOLS[s]
  if (!glyph) return null
  return { glyph, rotate: -ccw, flipX, flipY }
}

/** Render an FLTK label as text: '&' shortcut markers removed, symbols as their glyph. */
export function labelText(raw: string | undefined): string {
  if (!raw) return ''
  const sym = parseSymbol(raw)
  if (sym) return sym.glyph
  return raw.replace(/&&/g, '\u0000').replace(/&(?=.)/g, '').replace(/\u0000/g, '&').replace(/@@/g, '@')
}

/** Strip FLTK browser line format codes (@b, @C4, @. ...), noting bold/italic/center. */
export function browserCell(raw: string) {
  let s = raw
  let bold = false
  let italic = false
  let center = false
  let right = false
  for (;;) {
    const m = /^@(\.|@|[lmsbifcru-]|[BCFS]\d+)/.exec(s)
    if (!m || m[1] === '@') break
    s = s.slice(m[0].length)
    if (m[1] === '.') break
    if (m[1] === 'b') bold = true
    if (m[1] === 'i') italic = true
    if (m[1] === 'c') center = true
    if (m[1] === 'r') right = true
  }
  return { text: s.replace(/^@@/, '@'), bold, italic, center, right }
}

export function shortcutText(sc: number): string {
  if (!sc) return ''
  const parts: string[] = []
  if (sc & KEY.CTRL) parts.push('Ctrl')
  if (sc & KEY.ALT) parts.push('Alt')
  if (sc & KEY.SHIFT) parts.push('Shift')
  const key = sc & 0xffff
  if (key > KEY.F && key <= KEY.F + 35) parts.push(`F${key - KEY.F}`)
  else if (key >= 32 && key < 127) parts.push(String.fromCharCode(key).toUpperCase())
  return parts.join('+')
}

/** FLTK shortcut value for a browser keyboard event. */
export function shortcutOf(e: KeyboardEvent): number {
  const ctrl = e.ctrlKey || e.metaKey
  const mods = (ctrl ? KEY.CTRL : 0) | (e.altKey ? KEY.ALT : 0)
  const fm = /^F(\d+)$/.exec(e.key)
  if (fm) return KEY.F + Number(fm[1]) | mods | (e.shiftKey ? KEY.SHIFT : 0)
  if (e.key.length !== 1) return 0
  // With Ctrl/Alt, FLTK shortcuts use the lowercase key; plain keys are case sensitive ('c' vs 'C').
  if (mods) return e.key.toLowerCase().charCodeAt(0) | mods | (e.shiftKey ? KEY.SHIFT : 0)
  return e.key.charCodeAt(0)
}

export function modsOf(e: { ctrlKey: boolean; shiftKey: boolean; altKey: boolean; metaKey: boolean }) {
  return (e.shiftKey ? KEY.SHIFT : 0) | (e.ctrlKey || e.metaKey ? KEY.CTRL : 0) | (e.altKey ? KEY.ALT : 0)
}

export function isGray(hex: string) {
  const r = parseInt(hex.slice(1, 3), 16)
  const g = parseInt(hex.slice(3, 5), 16)
  const b = parseInt(hex.slice(5, 7), 16)
  return Math.max(r, g, b) - Math.min(r, g, b) < 24
}
