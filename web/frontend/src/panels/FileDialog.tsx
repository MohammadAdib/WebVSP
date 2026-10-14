import { useEffect, useState } from 'react'
import { api } from '../api/client'
import type { DirListing } from '../api/types'
import { useStore } from '../store'

const LAST_DIR_KEY = 'vspweb-last-dir'

function lastDir(): string | undefined {
  try {
    return localStorage.getItem(LAST_DIR_KEY) ?? undefined
  } catch {
    return undefined
  }
}

/** Extensions in an FLTK filter like "*.vsp3", "*.{af,dat}" or "Images (*.{jpg,png})"; empty = any. */
function filterExts(filter?: string): string[] {
  if (!filter) return []
  const exts: string[] = []
  for (const m of filter.matchAll(/\*\.(\{[^}]*\}|[\w]+)/g)) {
    const body = m[1]
    if (body.startsWith('{')) exts.push(...body.slice(1, -1).split(',').map((e) => e.trim().toLowerCase()))
    else exts.push(body.toLowerCase())
  }
  return exts.filter((e) => e && e !== '*')
}

function join(dir: string, name: string) {
  const sep = dir.includes('\\') || /^[A-Za-z]:/.test(dir) ? '\\' : '/'
  return dir.endsWith(sep) ? dir + name : dir + sep + name
}

export function FileDialog() {
  const req = useStore((s) => s.fileDialog)
  const setReq = useStore((s) => s.setFileDialog)
  const [listing, setListing] = useState<DirListing | null>(null)
  const [pathText, setPathText] = useState('')
  const [name, setName] = useState('')
  const [error, setError] = useState<string | null>(null)

  const load = (path?: string) =>
    api
      .listDir(path)
      .then((l) => {
        setListing(l)
        setPathText(l.path)
        setError(null)
        if (l.path)
          try {
            localStorage.setItem(LAST_DIR_KEY, l.path)
          } catch {
            /* storage unavailable */
          }
      })
      .catch((e) => setError(e.message))

  useEffect(() => {
    if (!req) return
    setName(req.mode === 'save' ? (req.defaultName ?? '') : '')
    const start = req.dir || lastDir()
    api
      .listDir(start)
      .catch(() => api.listDir())
      .then((l) => (start ? l : api.listDir(l.home)))
      .then((l) => {
        setListing(l)
        setPathText(l.path)
      })
      .catch((e) => setError(e.message))
  }, [req])

  if (!req) return null

  const finish = (path: string | null) => {
    setReq(null)
    req.resolve(path)
  }

  const exts = filterExts(req.filter)
  const matches = (file: string) => !exts.length || exts.some((e) => file.toLowerCase().endsWith('.' + e))

  const accept = () => {
    if (!listing || !name.trim()) return
    let file = name.trim()
    if (req.mode === 'save' && exts.length && !matches(file)) file += '.' + exts[0]
    finish(/^([A-Za-z]:|\/|\\)/.test(file) ? file : join(listing.path, file))
  }

  return (
    <div className="modal-backdrop" onPointerDown={(e) => e.target === e.currentTarget && finish(null)}>
      <div className="modal" onKeyDown={(e) => e.key === 'Escape' && finish(null)}>
        <div className="modal-head">
          {req.title}
          {req.filter && <span className="modal-filter">{req.filter}</span>}
        </div>
        <div className="modal-path">
          <button disabled={listing?.parent == null} onClick={() => void load(listing?.parent ?? undefined)} title="Up">
            ↑
          </button>
          <input
            value={pathText}
            onChange={(e) => setPathText(e.target.value)}
            onKeyDown={(e) => e.key === 'Enter' && void load(pathText)}
          />
          <button onClick={() => listing && void load(listing.home)}>Home</button>
        </div>
        <div className="modal-list">
          {error && <div className="empty">{error}</div>}
          {listing?.entries.map((e) => {
            const isVsp = matches(e.name)
            return (
              <div
                key={e.path}
                className={'entry' + (!e.dir && !isVsp ? ' dim' : '') + (!e.dir && e.name === name ? ' sel' : '')}
                onClick={() => !e.dir && setName(e.name)}
                onDoubleClick={() => (e.dir ? void load(e.path) : finish(e.path))}
              >
                <span className="ico">{e.dir ? '▸' : ''}</span>
                {e.name}
              </div>
            )
          })}
        </div>
        <div className="modal-foot">
          <input
            autoFocus
            placeholder="File name"
            value={name}
            onChange={(e) => setName(e.target.value)}
            onKeyDown={(e) => e.key === 'Enter' && accept()}
          />
          <button onClick={() => finish(null)}>Cancel</button>
          <button onClick={accept} disabled={!name.trim()}>
            {req.mode === 'save' ? 'Save' : 'Open'}
          </button>
        </div>
      </div>
    </div>
  )
}
