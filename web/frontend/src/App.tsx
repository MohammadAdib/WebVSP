import { useEffect, useRef, useState } from 'react'
import { connectEvents } from './api/client'
import type { ServerEvent } from './api/types'
import { DialogHost } from './mirror/Dialogs'
import { useMirror } from './mirror/store'
import type { MDialog, MirrorMsg, MWidget, MWindow } from './mirror/types'
import { SidePanel, usePanelHeight, ViewerOverlay } from './mirror/Dock'
import { FloatingWindow, MirrorMenuBar } from './mirror/Window'
import { FileDialog } from './panels/FileDialog'
import { setWindowTitle } from './platform'
import { useStore } from './store'
import { Viewport } from './viewport/Viewport'

function StatusBar() {
  const tree = useStore((s) => s.tree)
  const connected = useStore((s) => s.connected)
  const error = useStore((s) => s.error)
  const clearError = useStore((s) => s.clearError)
  return (
    <div className="statusbar">
      <span>{connected ? 'Connected' : 'Disconnected'}</span>
      <span>File Name: {tree?.fileName ?? ''}</span>
      {error && (
        <span className="err" title={error} onClick={clearError}>
          {error}
        </span>
      )}
      <span className="spacer" />
      <span>TitanVSP · {tree?.version?.replace(/^OpenVSP\s*/, 'VSP ')}</span>
    </div>
  )
}

const isDocked = (w: MWindow) => w.role === 'browser' || w.role === 'geom' || (w.role === 'screen' && w.screen === 'Vehicle')

export default function App() {
  const loadTree = useStore((s) => s.loadTree)
  const handleEvent = useStore((s) => s.handleEvent)
  const setConnected = useStore((s) => s.setConnected)
  const run = useStore((s) => s.run)
  const tree = useStore((s) => s.tree)
  const windows = useMirror((s) => s.windows)
  const mainRef = useRef<HTMLDivElement>(null)
  const panelHeight = usePanelHeight(mainRef)
  const [fullscreen, setFullscreen] = useState(false)
  const mirrorEnabled = useMirror((s) => s.enabled)

  useEffect(() => {
    void run(async () => {
      await loadTree()
      useStore.getState().requestView('leftiso')
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
    setWindowTitle(`${file} — TitanVSP`)
  }, [tree?.fileName])

  const all = Object.values(windows).sort((a, b) => a.id - b.id)
  const browserWin = all.find((w) => w.role === 'browser' && !w.closing) ?? all.find((w) => w.role === 'browser')
  const editorWin = all.find((w) => isDocked(w) && w.role !== 'browser' && !w.closing)
  const floating = all.filter((w) => w.role !== 'main' && !isDocked(w))
  const mainWin = all.find((w) => w.role === 'main')

  return (
    <div className="app">
      <div className="topbar">
        <span className="brand">TitanVSP</span>
        <MirrorMenuBar />
        {!mirrorEnabled && <span className="hint">GUI mirror unavailable (engine built without the GUI)</span>}
      </div>
      <div className="main" ref={mainRef}>
        <SidePanel side="left" win={browserWin} open={!fullscreen} height={panelHeight} />
        <div className="viewer">
          <Viewport />
          <ViewerOverlay fullscreen={fullscreen} onToggle={() => setFullscreen(!fullscreen)} />
        </div>
        <SidePanel side="right" win={editorWin} open={!fullscreen} height={panelHeight} />
        {floating.map((w, i) => (
          <FloatingWindow key={w.id} win={w} index={i} origin={mainWin} />
        ))}
      </div>
      <StatusBar />
      <DialogHost />
      <FileDialog />
    </div>
  )
}
