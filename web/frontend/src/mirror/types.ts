// Shapes produced by src/gui_and_draw/WebMirror.cpp.

export type WindowRole = 'main' | 'browser' | 'geom' | 'screen' | 'dialog'

export interface MWindow {
  id: number
  role: WindowRole
  screen: string
  si: number
  title: string
  w: number
  h: number
  /** Where VSP placed the window (screen coordinates) */
  x: number
  y: number
  /** The size the window was designed at (before any client resized it) */
  dw?: number
  dh?: number
  modal: boolean
  /** Set by the client while the window animates out after the GUI closed it */
  closing?: boolean
}

export interface MenuItem {
  i: number
  label: string
  d: number
  flags: number
  sc: number
}

export interface TreeItem {
  n: number
  label: string
  d: number
  open: boolean
  sel: boolean
  kids: number
  x: number
  y: number
  w: number
  h: number
  lx: number
  act: boolean
  show?: boolean
  surf?: number
  ref?: string
  showxy?: [number, number, number, number]
  surfxy?: [number, number, number, number]
}

export interface BrowserLine {
  t: string
  s?: boolean
  c?: boolean
  v?: boolean
}

export type Kind =
  | 'gl' | 'subwindow' | 'tabs' | 'scroll' | 'tree' | 'check_browser' | 'browser' | 'text_editor' | 'terminal'
  | 'text_display' | 'table' | 'canvas' | 'input_choice' | 'menubar' | 'choice' | 'menubutton' | 'counter'
  | 'value_slider' | 'scrollbar' | 'slider' | 'roller' | 'dial' | 'progress' | 'output' | 'input' | 'check'
  | 'round' | 'light' | 'repeat' | 'button' | 'box' | 'group' | 'widget'

export interface GlDrawObj {
  t: 'lines' | 'strip' | 'loop' | 'points'
  c: string
  pc: string
  lw: number
  ps: number
  /** x,y,z triples */
  p: number[]
}

export interface GlDraw {
  zoom: number
  px: number
  py: number
  /** 0: X,Y  1: X,Z  2: Z,Y */
  plane: number
  ppu: number
  objs: GlDrawObj[]
}

export interface MWidget {
  id: number
  win: number
  p: number
  o: number
  k: Kind
  x: number
  y: number
  w: number
  h: number
  vis: boolean
  act: boolean
  label?: string
  align: number
  box: number
  ls: number
  lf: number
  col: string
  lcol: string
  scol: string
  tip?: string
  parm?: string
  desc?: string
  // kind-specific
  v?: number | string
  type?: number
  min?: number
  max?: number
  step?: number
  ss?: number
  ro?: boolean
  ts?: number
  tcol?: string
  items?: MenuItem[] | TreeItem[]
  lines?: BrowserLine[]
  cols?: number[]
  csep?: string
  top?: number
  rows?: number
  ncols?: number
  img?: string
  iw?: number
  ih?: number
  draw?: GlDraw
}

export interface MDiff {
  t: 'diff'
  full: boolean
  windows: MWindow[]
  closed: number[]
  widgets: MWidget[]
  removed: number[]
}

export interface MDialog {
  t: 'dialog'
  seq: number
  kind: 'choice' | 'alert' | 'message' | 'file'
  msg?: string
  b0?: string
  b1?: string
  b2?: string
  title?: string
  filter?: string
  mode?: number
  preset?: string
  dir?: string
  /** The engine is showing the desktop's own dialog for this one */
  native?: boolean
}

export type MirrorMsg = MDiff | MDialog

export type MirrorEvent = Record<string, string | number>
