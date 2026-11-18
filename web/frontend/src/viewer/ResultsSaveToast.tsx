import { declineOffered, saveOffered, useResultsSave } from '../api/solverResults'

/** Asks to save a finished VSPAERO run's results next to the model, and says when they're saved. */
export function ResultsSaveToast() {
  const { ask, note, failed } = useResultsSave()
  const folderAccess = 'showDirectoryPicker' in window
  if (ask) {
    return (
      <div className="results-toast" role="dialog" aria-label="Save VSPAERO results">
        <div className="results-toast-text">
          Save the VSPAERO results next to <b>{ask.modelName}</b>?
          <span className="results-toast-hint">
            {ask.folder
              ? `To ${ask.folder.name}, as before: the browser asks to allow it once per visit.`
              : folderAccess
                ? 'Choose its folder once; later runs save there by themselves. File > Open the model with them for Load Previous Results.'
                : `${ask.files.length} files will be downloaded.`}
          </span>
        </div>
        <div className="results-toast-buttons">
          <button onClick={declineOffered}>Not now</button>
          <button className="primary" onClick={() => void saveOffered()}>
            {ask.folder ? 'Save' : folderAccess ? 'Save…' : 'Download'}
          </button>
        </div>
      </div>
    )
  }
  if (note) {
    return (
      <div className={'results-toast note' + (failed ? ' failed' : '')} role="status">
        {note}
      </div>
    )
  }
  return null
}
