import { useEffect, useRef, useState } from 'react'
import { connectEvents } from './api/client'
import type { ServerEvent } from './api/types'
import { DialogHost } from './mirror/Dialogs'
import { useMirror } from './mirror/store'
import type { MDialog, MirrorMsg, MWidget, MWindow } from './mirror/types'
import { SidePanel, usePanelHeight, ViewerOverlay } from './mirror/Dock'
import { FloatingWindow, MirrorMenuBar } from './mirror/Window'
import { useStore } from './store'
import { Viewport } from './viewport/Viewport'
import { ResultsViewer } from './viewer/ResultsViewer'
import { WasmToolbar } from './WasmToolbar'

/** The right end of the top bar: the last error (click to dismiss) and the OpenVSP version. */
function TopBarStatus() {
  const tree = useStore((s) => s.tree)
  const error = useStore((s) => s.error)
  const clearError = useStore((s) => s.clearError)
  return (
    <>
      <span className="spacer" />
      {error && (
        <span className="err" title={`${error} (click to dismiss)`} onClick={clearError}>
          {error}
        </span>
      )}
      {tree?.version && <span className="version">{tree.version.replace(/^OpenVSP\s*/, 'VSP ')}</span>}
    </>
  )
}

/** The model's file name, in the bottom-left corner of the 3D view. */
function FileName() {
  const name = useStore((s) => s.tree?.fileName)
  return name ? <div className="viewer-file" title={name}>{name}</div> : null
}

// The Geom Browser docks left and the geom editors right. Group Modifications (several geoms
// selected) floats like the desktop's, rather than taking the editor's place.
const isDocked = (w: MWindow) =>
  w.role === 'browser' ||
  (w.role === 'geom' && w.screen !== 'Group Modifications') ||
  (w.role === 'screen' && w.screen === 'Vehicle')

export default function App() {
  const loadTree = useStore((s) => s.loadTree)
  const handleEvent = useStore((s) => s.handleEvent)
  const setConnected = useStore((s) => s.setConnected)
  const run = useStore((s) => s.run)
  const tree = useStore((s) => s.tree)
  const windows = useMirror((s) => s.windows)
  const connected = useStore((s) => s.connected)
  const mainRef = useRef<HTMLDivElement>(null)
  const panelHeight = usePanelHeight(mainRef)
  const [fullscreen, setFullscreen] = useState(false)
  const mirrorEnabled = useMirror((s) => s.enabled)

  useEffect(() => {
    void run(async () => {
      await loadTree()
      // Every pane frames the model it starts with, from its own direction.
      useStore.getState().refitAll()
    })
    const conn = connectEvents(
      (msg) => {
        const m = useMirror.getState()
        if (msg.type === 'mirror_snapshot') {
          m.snapshot(msg as unknown as { enabled: boolean; windows: MWindow[]; widgets: MWidget[]; dialogs: MDialog[] })
        } else if (msg.type === 'mirror') {
          m.apply((msg as unknown as { msgs: MirrorMsg[] }).msgs)
        } else if (msg.type === 'dialog_done') {
          m.dialogDone((msg as unknown as { seq: number }).seq)
        } else {
          handleEvent(msg as ServerEvent)
        }
      },
      (c) => {
        setConnected(c)
        if (c) void run(loadTree)
      },
    )
    useMirror.getState().setSender((event) => conn.send({ type: 'mirror_event', event }))
    return conn.close
  }, [loadTree, handleEvent, setConnected, run])

  useEffect(() => {
    const file = tree?.fileName?.split(/[\\/]/).pop() || 'Unnamed.vsp3'
    document.title = `${file} — WebVSP`
  }, [tree?.fileName])

  const all = Object.values(windows).sort((a, b) => a.id - b.id)
  const browserWin = all.find((w) => w.role === 'browser' && !w.closing) ?? all.find((w) => w.role === 'browser')
  const editorWin = all.find((w) => isDocked(w) && w.role !== 'browser' && !w.closing)
  const floating = all.filter((w) => w.role !== 'main' && !isDocked(w))
  const mainWin = all.find((w) => w.role === 'main')

  return (
    <div className="app">
      {/* data-ready: OpenVSP has started (tests wait for it) */}
      <div className="topbar" data-ready={connected ? '1' : '0'}>
        <span className="brand">WebVSP</span>
        <MirrorMenuBar />
        <WasmToolbar />
        {!mirrorEnabled && connected && <span className="hint">GUI mirror unavailable (engine built without the GUI)</span>}
        <TopBarStatus />
      </div>
      <div className="main" ref={mainRef}>
        <SidePanel side="left" win={browserWin} open={!fullscreen} height={panelHeight} />
        <div className="viewer">
          <Viewport />
          <FileName />
          <ViewerOverlay fullscreen={fullscreen} onToggle={() => setFullscreen(!fullscreen)} />
        </div>
        <SidePanel side="right" win={editorWin} open={!fullscreen} height={panelHeight} />
        {floating.map((w, i) => (
          <FloatingWindow key={w.id} win={w} index={i} origin={mainWin} />
        ))}
        <ResultsViewer />
      </div>
      <DialogHost />
    </div>
  )
}
