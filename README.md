# WebVSP

[OpenVSP](https://openvsp.org), NASA's parametric aircraft geometry tool, in a web browser.

**Open it: https://mohammadadib.github.io/WebVSP/**

WebVSP is OpenVSP's own GUI, not a reimplementation. OpenVSP runs with its real FLTK GUI kept
off screen, and every window, tab, button, menu and prompt is mirrored into the page, so every
screen behaves exactly as it does on the desktop. The page draws them in a dark theme next to
its own three.js 3D view.

## Two ways to run it

| | In the browser | On your computer |
|---|---|---|
| Address | https://mohammadadib.github.io/WebVSP/ | https://mohammadadib.github.io/WebVSP/local/ |
| What runs OpenVSP | The browser tab (OpenVSP compiled to WebAssembly) | The WebVSP engine, a local program the page connects to |
| Install | Nothing | [WebVSP-Engine-win64.zip](https://github.com/MohammadAdib/WebVSP/releases/latest/download/WebVSP-Engine-win64.zip): unzip, double-click `WebVSP Engine.cmd` |
| VSPAERO | Yes, single-threaded, with the page's results viewer | Yes, multi-threaded, with the desktop viewer |
| STEP export | No | Yes |
| Files | Opened and saved through the browser | Native file dialogs |

The status bar shows which engine is in use and links to the other one.

## What's different from desktop OpenVSP

The model, screens and analyses are OpenVSP's. The 3D view is the page's own:

- Orbit, pan and zoom to the cursor; view toolbar; Blender numpad keys; orthographic or
  perspective.
- Click to select, Shift- or Ctrl-click to select several, click on nothing to deselect.
- Move arrows on the selection, snapped to a 1-unit grid; one undo per move.
- The desktop's selection boxes, cross sections, outlines, CG and other drawings.

See [CHANGES.md](CHANGES.md) for everything WebVSP adds.

## Build and develop

- [web/README.md](web/README.md): the local engine and the page (build, run, develop).
- [web/wasm/README.md](web/wasm/README.md): the in-browser engine.
- [AGENTS.md](AGENTS.md): how the code fits together, for contributors and coding agents.

To build desktop OpenVSP itself, see the
[OpenVSP repository](https://github.com/OpenVSP/OpenVSP) and its build instructions.

## Source and license

WebVSP is a fork of OpenVSP 3.55.0 ([CHANGELOG.md](CHANGELOG.md) is OpenVSP's history).
Everything outside `web/` is OpenVSP, with small changes for the web GUI. OpenVSP is
available under the NASA Open Source Agreement (NOSA) version 1.3; see [LICENSE](LICENSE).
