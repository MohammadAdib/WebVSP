import { useEffect } from 'react'
import { create } from 'zustand'
import { useStore } from '../store'

/** X / Delete: the geoms waiting on "are you sure", and what deleting them does. */
const useDeleteAsk = create<{ names: string[]; onDelete: (() => void) | null }>(() => ({ names: [], onDelete: null }))

/** Asks before deleting the selection; onDelete runs if the answer is yes. */
export function askDelete(onDelete: () => void) {
  const { tree, geomsById } = useStore.getState()
  const sel = tree?.active ?? []
  if (!sel.length) return
  useDeleteAsk.setState({ names: sel.map((id) => geomsById[id]?.name ?? id), onDelete })
}

const close = () => useDeleteAsk.setState({ names: [], onDelete: null })

/** The prompt: Enter or Delete deletes, Esc or Cancel keeps them. No other key gets through. */
export function DeleteConfirm() {
  const { names, onDelete } = useDeleteAsk()
  const open = !!onDelete
  useEffect(() => {
    if (!open) return
    const onKey = (e: KeyboardEvent) => {
      e.stopPropagation()
      if (e.key === 'Escape') {
        e.preventDefault()
        close()
      } else if (e.key === 'Enter' && !(e.target instanceof HTMLButtonElement && e.target.dataset.cancel)) {
        e.preventDefault()
        onDelete?.()
        close()
      }
    }
    window.addEventListener('keydown', onKey, true)
    return () => window.removeEventListener('keydown', onKey, true)
  }, [open, onDelete])
  if (!open) return null
  const what = names.length === 1 ? names[0] : `${names.length} geoms`
  return (
    <div className="modal-backdrop" onPointerDown={(e) => e.target === e.currentTarget && close()}>
      <div className="modal prompt" role="alertdialog" aria-label="Delete">
        <div className="modal-head">Delete</div>
        <div className="prompt-msg">
          Delete {what}?
          {names.length > 1 && <div className="prompt-list">{names.join(', ')}</div>}
        </div>
        <div className="modal-foot">
          <span style={{ flex: 1 }} />
          <button data-cancel="1" onClick={close}>
            Cancel
          </button>
          <button
            className="danger"
            autoFocus
            onClick={() => {
              onDelete?.()
              close()
            }}
          >
            Delete
          </button>
        </div>
      </div>
    </div>
  )
}
