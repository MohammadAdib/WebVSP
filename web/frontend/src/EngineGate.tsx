import { useEffect, useState } from 'react'
import { engineAddress, isHostedUI } from './api/client'
import { useStore } from './store'

const REPO = 'https://github.com/MohammadAdib/WebVSP'

/**
 * On a hosted copy of the UI (GitHub Pages), OpenVSP itself runs on the user's machine.
 * While that engine can't be reached, explain how to start it; the socket keeps retrying.
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
          <p>This page is the TitanVSP interface. OpenVSP itself runs on your computer.</p>
          <ol>
            <li>
              Get <a href={REPO} target="_blank" rel="noreferrer">WebVSP</a> and build it once with{' '}
              <code>web\scripts\build_vsp.ps1</code>.
            </li>
            <li>
              Double-click <code>web\vsp-web.cmd</code> and leave its window open.
            </li>
            <li>If your browser asks to allow access to devices on your local network, allow it.</li>
          </ol>
          <p className="engine-wait">
            <span className="engine-dot" /> Looking for the engine at <code>{engineAddress}</code>…
          </p>
        </div>
      </div>
    </div>
  )
}
