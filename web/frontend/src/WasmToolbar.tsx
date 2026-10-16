import { useStore } from './store'

/** While OpenVSP downloads and starts. */
export function WasmToolbar() {
  const connected = useStore((s) => s.connected)
  if (connected) return null
  return <span className="hint">Loading OpenVSP in your browser…</span>
}
