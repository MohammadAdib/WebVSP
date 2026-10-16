/**
 * File > New Window: another WebVSP window with a model of its own. Every tab runs its own
 * OpenVSP, so a new tab is a new window.
 */
export function openNewWindow() {
  window.open(window.location.href, '_blank')
}
