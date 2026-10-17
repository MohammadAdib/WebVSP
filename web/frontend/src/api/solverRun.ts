import { create } from 'zustand'

interface Run {
  start: number | null
  end: number | null
  code: number | null
}

/**
 * The last VSPAERO run, timed in the VSPAERO window's title bar: when it started, and when and
 * how it ended (null while running). The run is OpenVSP's whole solver step (writing the
 * geometry, `vspaero`, then `vsploads`): it lasts while the window's Kill Solver button is active
 * (`setSolverRunning`, fed from the mirror). How it ended comes from `vspaero`'s exit code
 * (`solverExited`, from wasmEngine.ts; 143 when it's killed).
 */
export const useSolverRun = create<Run>(() => ({ start: null, end: null, code: null }))

/** The run going on: whether vspaero has been launched in it, its exit code once it has exited,
 *  and the last finished run (shown again if this one turns out not to be a run). */
let spawned = false
let exitCode: number | null = null
let previous: Run = { start: null, end: null, code: null }

/** Called when a run ends, with vspaero's exit code. */
const endListeners = new Set<(code: number) => void>()
export const onSolverEnded = (listener: (code: number) => void) => endListeners.add(listener)

export const solverSpawned = () => {
  spawned = true
}
export const solverExited = (code: number) => {
  exitCode = code
}

export function setSolverRunning(running: boolean) {
  const run = useSolverRun.getState()
  const timing = run.start !== null && run.end === null
  if (running && !timing) {
    previous = run
    useSolverRun.setState({ start: Date.now(), end: null, code: null })
  } else if (!running && timing) {
    // Kill Solver lit without vspaero being launched (the window appearing): not a run.
    // (OpenVSP launches vspaero just before it lights the button, so these are cleared after a
    // run, not at its start.)
    const code = exitCode ?? 0
    const real = spawned
    if (!real) useSolverRun.setState(previous)
    else useSolverRun.setState({ end: Date.now(), code })
    spawned = false
    exitCode = null
    if (real) endListeners.forEach((l) => l(code))
  }
}
