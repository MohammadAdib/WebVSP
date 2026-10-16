# WebVSP

[OpenVSP](https://openvsp.org), NASA's parametric aircraft geometry tool, in a web browser.

**Open it: https://mohammadadib.github.io/WebVSP/**

Nothing to install. OpenVSP itself, compiled to WebAssembly, runs in the browser tab.

WebVSP is OpenVSP's own GUI, not a reimplementation. OpenVSP runs with its real FLTK GUI kept
off screen, and every window, tab, button, menu and prompt is mirrored into the page, so every
screen behaves exactly as it does on the desktop. The page draws them in a dark theme next to
its own three.js 3D view.

## What you get

- OpenVSP's screens, menus and analyses, including VSPAERO (single-threaded) with a results
  viewer in the page.
- A 3D view with orbit, pan and zoom to the cursor, a view toolbar, Blender numpad keys and
  orthographic or perspective views.
- Click to select, Shift- or Ctrl-click to select several, click on nothing to deselect.
- Move arrows on the selection, snapped to a 1-unit grid, with one undo per move.
- The desktop's selection boxes, cross sections, outlines, CG and other drawings.
- Files are opened and saved through the browser. Each tab is its own window with its own model.

Not available yet: STEP export, multithreaded VSPAERO.

See [CHANGES.md](CHANGES.md) for everything WebVSP adds.

## Build and develop

- [web/README.md](web/README.md): building, running and deploying the web app.
- [web/wasm/README.md](web/wasm/README.md): the WebAssembly build of OpenVSP.
- [AGENTS.md](AGENTS.md): how the code fits together, for contributors and coding agents.

To build desktop OpenVSP itself, see the
[OpenVSP repository](https://github.com/OpenVSP/OpenVSP) and its build instructions.

## Source and license

WebVSP is a fork of OpenVSP 3.55.0 ([CHANGELOG.md](CHANGELOG.md) is OpenVSP's history).
Everything outside `web/` is OpenVSP, with small changes for the web app. OpenVSP is available
under the NASA Open Source Agreement (NOSA) version 1.3; see [LICENSE](LICENSE).
