import { api, isHostedUI } from './api/client'

/**
 * File > New Window: another WebVSP window with a model of its own. OpenVSP has one model
 * per process, so the engine starts another engine and the new tab talks to that one.
 */
export async function openNewWindow() {
  // Open the tab now, while the click still counts as a user action (popup blockers),
  // and point it at the new engine once that has started.
  const tab = window.open('', '_blank')
  if (tab) {
    tab.document.title = 'WebVSP'
    tab.document.body.style.cssText =
      'margin:0;height:100vh;display:grid;place-items:center;background:#1e1e1e;color:#bbb;font:14px system-ui,sans-serif'
    tab.document.body.textContent = 'Starting a new WebVSP window…'
  }
  try {
    const { port } = await api.newInstance()
    const url = isHostedUI
      ? `${location.origin}${location.pathname}?engine=${encodeURIComponent(`http://localhost:${port}`)}`
      : `${location.protocol}//${location.hostname}:${port}/`
    if (tab) tab.location.href = url
    else window.open(url, '_blank')
  } catch (err) {
    if (tab) tab.document.body.textContent = `Couldn't start a new window: ${err instanceof Error ? err.message : err}`
  }
}
