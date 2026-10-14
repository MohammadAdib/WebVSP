# WebVSP

A browser-based GUI for OpenVSP. `vsp-web` starts a local Python backend (FastAPI) that
runs OpenVSP's own FLTK GUI off screen and mirrors it to the browser: every screen, tab,
button and popup is the real one, drawn by a TypeScript + React frontend (Vite) in a dark
theme, next to a three.js 3D view.

```
web/
  backend/vspweb/   FastAPI app, OpenVSP thread + GUI mirror relay, tessellation
  frontend/         Vite + React + TypeScript UI
  scripts/          build helpers
```

## Build (Windows)

Needs Visual Studio 2022 (C++ workload), CMake >= 3.24, SWIG, Python 3.10+ and Node LTS.

```powershell
winget install SWIG.SWIG OpenJS.NodeJS.LTS
web\scripts\build_vsp.ps1
```

This builds the dependency libraries and the headless OpenVSP Python module into `build/`,
then creates `web/.venv` with `openvsp` and `vspweb` installed and builds the frontend.

## Run

Double-click `web\vsp-web.cmd` (or drag a `.vsp3` onto it). Each time, it:

1. kills anything on the GUI ports (old servers on :8765, Vite on :5173),
2. rebuilds the UI if any frontend source changed,
3. starts a fresh server and opens a new tab at **http://localhost:8765**.

Close the console window to stop it.

- From a shell: `web\.venv\Scripts\vsp-web [model.vsp3] [--no-browser] [--port N]`.
- There's no login. The server only listens on 127.0.0.1 and refuses requests whose
  Host or Origin isn't this localhost server, so other websites can't drive it.
  `--token` adds a random session token (printed as `VSPWEB_READY <port> <token>`)
  for desktop shells that want it.

## Hosted UI (GitHub Pages)

The UI is also published at **https://mohammadadib.github.io/WebVSP/** by
`.github/workflows/pages.yml` on every push to `main` that touches `web/frontend`.
OpenVSP still runs locally; the page connects to the engine at `http://localhost:8765`.

**Users (no build tools needed):** download the portable engine,
[WebVSP-Engine-win64.zip](https://github.com/MohammadAdib/WebVSP/releases/latest/download/WebVSP-Engine-win64.zip),
unzip it, double-click `WebVSP Engine.cmd`. The hosted page links to it until it connects.

The zip is built and published by `.github/workflows/engine.yml` (rolling `engine-latest`
release) on every push to `main` that touches the engine, the UI or `src/`. Build it locally
with `web\scripts\package_engine.ps1` after `build_vsp.ps1`.

Developers can also run the engine from a source build with `websp-web.cmd`.

- The engine only accepts the hosted UI from origins listed in `WEB_ORIGINS`
  (`web/backend/vspweb/app.py`, default `https://mohammadadib.github.io`), plus any in
  `WEBVSP_WEB_ORIGINS` (comma separated). Only add sites you control: an allowed site
  can open and save files on the machine running the engine.
- Browsers allow an https page to reach `http://localhost`; Chrome asks once for
  permission to access the local network.
- Point the page at another port with `?engine=http://localhost:PORT` (remembered).

## Develop

```powershell
web\vsp-web.cmd --no-browser          # backend on :8765 (any normal launch works)
cd web\frontend; npm run dev           # UI with hot reload at http://localhost:5173
web\.venv\Scripts\python -m pytest web\backend
```

Vite proxies `/api` (HTTP and WebSocket) to :8765 and rewrites the Origin header, so the
backend's same-origin check accepts it. Run `npm run build` to update what :8765 serves.

## Design notes

- **GUI mirror** (`src/gui_and_draw/WebMirror.*`). OpenVSP runs with its FLTK GUI, but
  windows stay hidden. Every shown window's widget tree (kind, label, rect, value, state,
  bound Parm) is diffed and streamed to the browser, which draws each widget at its exact
  FLTK position (`frontend/src/mirror/`). Input goes back as events applied to the real
  widgets (set value + callback, menu picks, or replayed mouse events for trees/canvases),
  so every screen's own logic runs unchanged.
- **Prompts** (`fl_choice`/`fl_alert`/`fl_message`, file choosers) go through
  `VspDialogs.*` / `ScreenMgr::FileChooser` and are answered by the browser. With no
  browser connected, choices and file prompts are cancelled so the backend never blocks.
- **Threading.** One thread (`vspweb/runtime.py`) owns OpenVSP and FLTK; it runs API jobs
  and pumps the GUI between them. Mirror input/output queues are thread-safe.
- **3D view.** Meshes come from the same u/w tessellation the FLTK GUI draws
  (`GetUWTess01` + `CompVecPnt01`/`CompVecNorm01`), sent as a compact binary payload
  (`vspweb/tess.py`). A watcher refetches only geoms whose Parms changed and follows each
  geom's draw type (Wire/Hidden/Shade) and the GUI's selection.
- Docking: Geom Browser and the active geom editor sit on the right like the FLTK app;
  all other screens and popups are floating windows. View-menu camera items drive the
  three.js view.
- Host-specific features (file dialogs, window title) go through `frontend/src/platform.ts`
  so a desktop wrapper can replace them.
