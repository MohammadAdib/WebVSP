# WebVSP: the web app

OpenVSP in a web browser. OpenVSP and its FLTK GUI are compiled to WebAssembly and run in the
tab; every OpenVSP window, tab, button and prompt is mirrored into a TypeScript + React page
(Vite) in a dark theme, next to the page's own three.js 3D view.

Live at **https://mohammadadib.github.io/WebVSP/**.

```
web/
  frontend/   the page: Vite + React + TypeScript, three.js (@react-three/fiber, drei), zustand
  wasm/       the engine: OpenVSP compiled to WebAssembly, headless FLTK, build scripts
```

## Build and run

Build the engine once (see [wasm/README.md](wasm/README.md) for the tools), then the page:

```powershell
web\wasm\build_wasm.ps1               # -> web\frontend\public\wasm\ (after C++ changes: -Stage vsp)
cd web\frontend; npm ci; npm run dev   # http://localhost:5173/ with hot reload
```

- `npx tsc -b` type-checks the page; `npm run build` writes `web/frontend/dist`.
- `web\wasm\serve.ps1 -Open` builds the page and serves the production build at
  http://localhost:4173/.
- Hard-refresh after rebuilding the engine: the browser caches the module.

## Deploy

Push to `main`; that's all. `.github/workflows/pages.yml` builds the engine (OpenVSP for
WebAssembly, on Linux), builds the page with it, and deploys both to GitHub Pages, on every push
that touches `src/`, `Libraries/`, `web/` or the workflow. Nothing needs building or uploading
by hand, so it works from any clone. The libraries are cached and OpenVSP compiles through
ccache, so ordinary pushes rebuild only what changed; the first run, or one that changes the
libraries, takes much longer.

## Design notes

- **GUI mirror** (`src/gui_and_draw/WebMirror.*`). OpenVSP runs with its FLTK GUI, but windows
  are never displayed. Every shown window's widget tree (kind, label, rect, value, state, bound
  Parm) is diffed and sent to the page, which draws each widget at its exact FLTK position
  (`frontend/src/mirror/`). Input goes back as events applied to the real widgets (set value +
  callback, menu picks, or replayed mouse events for trees and canvases), so every screen's own
  logic runs unchanged.
- **Prompts** (`fl_choice`/`fl_alert`/`fl_message`, file choosers) go through `VspDialogs.*` /
  `ScreenMgr::FileChooser` and are answered by the page; file choosers use the browser's file
  pickers.
- **Threading.** OpenVSP runs on its own thread in the module; the page talks to it through a
  thread-safe request queue (`web/wasm/engine/engine.cpp`, `frontend/src/api/wasmEngine.ts`).
- **3D view.** Meshes come from the same u/w tessellation the FLTK GUI draws, sent as a compact
  binary payload with each geom's outline (format in `frontend/src/api/mesh.ts`); Mesh and
  NGon Mesh geoms come per tag in the desktop's colours. A watcher
  refetches only geoms whose Parms changed and follows each geom's draw type and the GUI's
  selection. Selection boxes, cross sections, the CG and other screens' drawings are mirrored
  from OpenVSP's own draw objects.
- **Docking.** The Geom Browser sits on the left and the active geom editor on the right; all
  other screens (Group Modifications included) and popups are floating windows.

[AGENTS.md](../AGENTS.md) has the full architecture.
