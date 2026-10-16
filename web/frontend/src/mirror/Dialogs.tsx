import { sendMirror, useMirror } from './store'
import type { MDialog } from './types'

/**
 * Prompts raised by the GUI (fl_choice / fl_alert / fl_message). File choosers are answered
 * with the browser's file pickers by the engine (wasmEngine.ts).
 */
export function DialogHost() {
  const dialog = useMirror((s) => s.dialogs[0])
  if (!dialog || dialog.kind === 'file') return null
  const buttons = dialog.kind === 'choice' ? [dialog.b0, dialog.b1, dialog.b2] : ['OK']
  return (
    <div className="modal-backdrop">
      <div className="modal prompt" role="dialog">
        <div className="modal-head">{dialog.kind === 'alert' ? 'Alert' : dialog.kind === 'message' ? 'Message' : 'WebVSP'}</div>
        <div className="prompt-msg">{dialog.msg}</div>
        <div className="modal-foot">
          <span style={{ flex: 1 }} />
          {buttons.map((b, i) =>
            b ? (
              <button key={i} autoFocus={i === buttons.length - 1} onClick={() => answer(dialog, { result: i })}>
                {b.replace(/&/g, '')}
              </button>
            ) : null,
          )}
        </div>
      </div>
    </div>
  )
}

function answer(d: MDialog, r: { result?: number; path?: string }) {
  useMirror.getState().dialogDone(d.seq)
  sendMirror({ op: 'dialog', seq: d.seq, result: r.result ?? 0, path: r.path ?? '' })
}
