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

const SYMBOLS: Record<string, string> = {
  '<': '◀', '>': '▶', '<<': '«', '>>': '»', '->': '→', '<-': '←', '|<': '⏮', '>|': '⏭',
  '||': '⏸', '[]': '■', 'circle': '●', 'square': '■', 'line': '—', 'menu': '☰', 'search': '⌕',
  '+': '+', 'UpArrow': '▲', 'DnArrow': '▼', '2>': '▼', '8>': '▲', '4>': '◀', '6>': '▶',
  '2<': '▲', '8<': '▼', '2->': '↓', '8->': '↑', 'reload': '↻', 'undo': '↶', 'redo': '↷',
}

/** Render an FLTK label: '&' shortcut markers removed, '@' symbol labels mapped to glyphs. */
export function labelText(raw: string | undefined): string {
  if (!raw) return ''
  if (raw.startsWith('@') && !raw.startsWith('@@')) {
    let sym = raw.slice(1).replace(/^[#+-]?\d?(?=\D)/, '')
    if (SYMBOLS[sym]) return SYMBOLS[sym]
    sym = sym.replace(/^\d/, (d) => d) // orientation digit kept for lookups like "8>"
    if (SYMBOLS[sym]) return SYMBOLS[sym]
    const bare = sym.replace(/^\d/, '')
    return SYMBOLS[bare] ?? bare
  }
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
