import { useEffect, useState } from 'react'
import { engineAddress, isHostedUI } from './api/client'
import { useStore } from './store'

const DOWNLOAD = 'https://github.com/MohammadAdib/WebVSP/releases/latest/download/TitanVSP-Engine-win64.zip'

/**
 * On a hosted copy of the UI (GitHub Pages), OpenVSP itself runs on the user's machine.
 * While that engine can't be reached, offer the download; the socket keeps retrying, so the
 * page connects on its own as soon as the engine starts.
 */
export function EngineGate() {
  const connected = useStore((s) => s.connected)
  const [grace, setGrace] = useState(true)
  useEffect(() => {
    const t = window.setTimeout(() => setGrace(false), 1500)
    return () => window.clearTimeout(t)
  }, [])
  if (!isHostedUI || connected || grace) return null
  return (
    <div className="modal-backdrop engine-gate">
      <div className="modal prompt">
        <div className="modal-head">Start the TitanVSP engine</div>
        <div className="prompt-msg">
          <p>TitanVSP runs OpenVSP on your computer. Start the engine once and this page connects by itself.</p>
          <a className="engine-download" href={DOWNLOAD}>
            <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.6">
              <path d="M8 2v8M4.5 7 8 10.5 11.5 7M3 13.5h10" />
            </svg>
            Download TitanVSP Engine for Windows
          </a>
          <ol>
            <li>Unzip it and double-click <code>TitanVSP Engine</code>. Keep its window open.</li>
            <li>If the browser asks to allow access to your local network, choose Allow.</li>
          </ol>
          <p className="engine-wait">
            <span className="engine-dot" /> Waiting for the engine at <code>{engineAddress}</code>
          </p>
        </div>
      </div>
    </div>
  )
}
