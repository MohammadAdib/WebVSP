import { useEffect, useRef, useState } from 'react'
import { connectEvents } from './api/client'
import type { ServerEvent } from './api/types'
import { useBlenderKeys } from './mirror/blenderKeys'
import { DeleteConfirm } from './mirror/DeleteConfirm'
import { DialogHost } from './mirror/Dialogs'
import { useMirror } from './mirror/store'
import type { MDialog, MirrorMsg, MWidget, MWindow } from './mirror/types'
import { SidePanel, usePanelHeight, ViewerOverlay } from './mirror/Dock'
import { FloatingWindow, MirrorMenuBar } from './mirror/Window'
import { useStore } from './store'
import { Viewport } from './viewport/Viewport'
import { ResultsViewer } from './viewer/ResultsViewer'
import { WasmToolbar } from './WasmToolbar'

/** WebVSP's source, linked from the top bar. */
const REPO_URL = 'https://github.com/MohammadAdib/WebVSP'

/** The right end of the top bar: the last error (click to dismiss), the OpenVSP version and a
 *  link to WebVSP's repository. */
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
      <a className="repo-link" href={REPO_URL} target="_blank" rel="noopener noreferrer" title="WebVSP on GitHub" aria-label="WebVSP on GitHub">
        <svg width="16" height="16" viewBox="0 0 16 16" fill="currentColor" aria-hidden="true">
          <path d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0016 8c0-4.42-3.58-8-8-8z" />
        </svg>
      </a>
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
  useBlenderKeys()

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
    const file = tree?.fileName?.split(/[\\/]/).pop()
    // No file yet (a model never saved): just the app's name.
    document.title = file ? `${file} — WebVSP` : 'WebVSP'
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
      <DeleteConfirm />
    </div>
  )
}
