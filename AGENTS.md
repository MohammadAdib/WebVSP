# AGENTS.md: how WebVSP is put together

A guide for coding agents and contributors. Read this before changing anything; it explains
where things live, how data moves, and the traps that are easy to fall into.

WebVSP is a fork of OpenVSP 3.55.0 that puts OpenVSP's **own GUI** in a web page. Nothing in
the page reimplements an OpenVSP screen: OpenVSP runs with its real FLTK GUI kept off screen,
and a mirror serializes every window's widget tree to the page and applies the page's input
back to the real widgets. The page adds its own 3D view (three.js), a VSPAERO results viewer
and the plumbing to run OpenVSP in two different engines.

What changed and when: [CHANGES.md](CHANGES.md). User-facing docs: [README.md](README.md),
[web/README.md](web/README.md), [web/wasm/README.md](web/wasm/README.md).

---

## 1. The big picture

```
                         ┌──────────────────────────── the page (web/frontend) ─────────────────────────────┐
                         │  mirror/      OpenVSP's windows, drawn from widget JSON; input → events          │
                         │  viewport/    three.js 3D view: meshes, outlines, overlay, move gizmo            │
                         │  viewer/      VSPAERO results viewer (.adb)                                      │
                         │  api/client.ts  one API, two transports ─────────────┬──────────────────────────┤
                         └──────────────────────────────────────────────────────┼──────────────────────────┘
                                         HTTP + WebSocket                       │  function calls
                                                ▼                               ▼
   ┌──────────── local engine (web/backend, Python) ─────────────┐   ┌──── in-browser engine (web/wasm) ────┐
   │ FastAPI app.py → session.py → vsp_worker → runtime.py thread │   │ wasmEngine.ts → vsp.wasm             │
   │                 mirror.py relays GUI output/input            │   │ engine.cpp: OpenVSP on a pthread      │
   └──────────────────────────────┬───────────────────────────────┘   └───────────────┬──────────────────────┘
                                  ▼                                                   ▼
                ┌────────────────────────── OpenVSP (src/, C++) ────────────────────────────┐
                │ geom_core (the model)  geom_api (VSP_Geom_API + Mirror* calls)              │
                │ gui_and_draw (FLTK screens) + WebMirror (the mirror)  util (ProcessUtil...) │
                └─────────────────────────────────────────────────────────────────────────────┘
```

Both engines expose **the same operations** to the page (same routes, same JSON, same binary
mesh format, same GUI mirror messages). `api/client.ts` picks the transport; everything above
it doesn't care which engine is running.

### Which engine a page uses

Decided by the address alone (`chooseWasm()` in `web/frontend/src/api/wasmEngine.ts`); nothing
is remembered:

| Page | Engine |
|---|---|
| `https://mohammadadib.github.io/WebVSP/` | in-browser (WebAssembly) |
| `https://mohammadadib.github.io/WebVSP/local/` | local engine at `http://localhost:8765` |
| `http://localhost:5173/` (Vite dev server) | in-browser; `/local/` proxies to the local engine |
| `http://localhost:8765/` (served by an engine) | that engine |
| `?engine=wasm` / `?engine=local` / `?engine=http://localhost:PORT` | overrides (PORT: File > New Window) |

`/local/` is a copy of `index.html` written at build time by the `localPage` plugin in
`web/frontend/vite.config.ts`.

---

## 2. Repository map

Everything outside `web/` is OpenVSP. WebVSP's changes there are small and targeted; most of
the work lives in a handful of files.

### `web/` (all WebVSP)

| Path | What |
|---|---|
| `web/backend/vspweb/` | The local engine (Python package `vspweb`, FastAPI). |
| `web/backend/tests/` | Backend tests (pytest): API, meshes, undo, security, end-to-end mirror. |
| `web/frontend/` | The page: Vite + React 19 + TypeScript, three.js via @react-three/fiber and drei, zustand stores. |
| `web/wasm/` | The in-browser engine: build scripts, headless FLTK, `engine/engine.cpp`. |
| `web/scripts/build_vsp.ps1` | Builds libraries + OpenVSP Python modules + venv + UI (Windows). |
| `web/scripts/package_engine.ps1` | Builds the portable engine zip. |
| `web/vsp-web.cmd`, `vsp-web.ps1` | One-click launcher for a source build. |
| `.github/workflows/pages.yml` | Builds the page, adds the WebAssembly engine from the `wasm-latest` release, deploys to Pages. |
| `.github/workflows/engine.yml` | Builds the Windows engine zip and publishes the rolling `engine-latest` release. |

### Local engine: `web/backend/vspweb/`

| File | Role |
|---|---|
| `__main__.py` | `vsp-web` entry: port handling, opening the browser, `--exit-when-idle`, prints `VSPWEB_READY <port> <token>`. |
| `runtime.py` | **The OpenVSP thread.** Imports `openvsp`, owns the model and FLTK, runs queued jobs, then `vsp.MirrorPump(0.005)` between them, plus periodic callbacks. Import `vsp` from here, never `openvsp` directly. |
| `vsp_worker.py` | `VspWorker.call(fn, ...)`: run on the OpenVSP thread from asyncio; drains OpenVSP's error stack into `VspError` (→ HTTP 400). |
| `session.py` | The editing session: every operation, change detection (`_watch` every ~150 ms; per-geom parm fingerprints), `model_changed` events, mesh cache. |
| `app.py` | FastAPI routes under `/api`, the `/api/ws` WebSocket, origin/host/token guard, CORS + Private Network Access for trusted hosted origins (`WEB_ORIGINS`). |
| `mirror.py` | Relays WebMirror output to clients (keeps a full snapshot for new clients), client input back to the GUI; native file dialogs and the remembered folder. |
| `tess.py` | Meshes: u/w tessellation → binary `VSPM` payload (format in its docstring). |
| `parms.py`, `choices.py` | Parm schema/values for the API (enum labels for integer parms). |
| `undo.py` | The backend's own undo stack for `/api/undo`. **The page doesn't use it**: Ctrl+Z / Edit > Undo is OpenVSP's own undo through the mirrored menu. |
| `instances.py` | File > New Window: spawns another engine on a free port. |

### In-browser engine: `web/wasm/`

| Path | Role |
|---|---|
| `build_wasm.ps1` | `-Stage libs|fltk|vsp|all`, `-Targets vsp_wasm,vspaero,vsploads`. Output → `web/frontend/public/wasm/` (gitignored). |
| `toolchain.cmake` | Emscripten flags: everything with `-pthread -fwasm-exceptions`. |
| `fltk/` + `patch_fltk.py` | A "headless" FLTK platform added to FLTK's sources: windows lay out but are never shown; non-blocking wait; text measured with canvas `measureText`. |
| `engine/engine.cpp` | `main()` on a pthread (`PROXY_TO_PTHREAD`): request queue, change watcher (port of `session._watch`), GUI pump. Exports `call`, `take`, `takeBinary`, `mirrorEvent`, `requestFull`, `processOutput`, `processExit`. The request list is in its header comment. |
| `engine/gl_stub.c` | Do-nothing OpenGL, generated by `gen_gl_stub.py` from `gl_symbols.txt`. The GUI never draws with GL here. |
| `engine/CMakeLists.txt` | Link options for `vsp_wasm` and the `vspaero`/`vsploads` worker programs. |
| `serve.ps1` | Builds the UI and serves it with the right headers on :4173. |

Page side: `web/frontend/src/api/wasmEngine.ts` (transport, file pickers, program workers,
the web VSPAERO viewer launch), `public/coi-sw.js` (service worker for cross-origin isolation;
SharedArrayBuffer needs it), `public/program.worker.js` (runs one vspaero/vsploads per run).

### The page: `web/frontend/src/`

| Path | Role |
|---|---|
| `main.tsx` | Registers the isolation service worker (in-browser engine), mounts `App`. |
| `App.tsx` | Layout: top bar (mirrored menu), left panel (Geom Browser), 3D view, right panel (geom editor), floating windows, results viewer, status bar with the engine switch. `isDocked()` decides docked vs floating. |
| `store.ts` | Model store (zustand): tree, `geomsById`, `meshRev` (bumped per geom on change → mesh refetch), `valuesRev`, view requests, ortho. |
| `api/client.ts` | `api.*` calls and `connectEvents()`; HTTP/WS or `wasmRequest`/`wasmConnect`. |
| `api/mesh.ts` | Decoder for the `VSPM` mesh payload (+ trailing outline section). |
| `api/wasmEngine.ts` | In-browser transport (see above). |
| `mirror/` | The GUI mirror client: `types.ts` (message shapes), `store.ts` (windows, widgets, overlay), `Window.tsx` (floating windows, menus, z-order), `Dock.tsx` (side panels), `Widgets.tsx` (one component per FLTK widget kind), `Custom.tsx` (GL sub-views and images), `Dialogs.tsx` (prompts), `fltk.ts` (FLTK constants, symbol labels, shortcuts). |
| `viewport/Viewport.tsx` | Main 3D view: `GeomMesh` (surfaces, wireframe, outline), `Overlay` (mirrored draw objects), `BlankMarkers`, `MoveGizmo`, click selection. Tunables at the top (`BBOX_ALPHA`, `OUTLINE_WIDTH`, `MOVE_SNAP`, ...). |
| `viewport/camera.tsx` | Camera shared with the results viewer: perspective/ortho with pose carry-over, orbit/pan/zoom-to-cursor, fit, view keys (Blender numpad), view toolbar. |
| `viewer/` | VSPAERO results viewer: `adb.ts` (binary `.adb` reader, `.adb.cases`, `.polar`), `store.ts`, `ResultsViewer.tsx` (floating window, colour legend, case/coefficients panel). |
| `panels/FileDialog.tsx` | In-page file browser (local engine fallback when no native dialog). |
| `platform.ts` | Host hooks (file pickers, window title) a desktop wrapper could replace. |

### OpenVSP (C++) pieces WebVSP added or changed

| File | Why |
|---|---|
| `src/gui_and_draw/WebMirror.{h,cpp}` | **The mirror.** Widget-tree JSON diffs, input events, GL-view draw objects, main-view overlay, prompts, `PickGeom`. ~1.7k lines; the heart of the GUI side. |
| `src/gui_and_draw/VspDialogs.{h,cpp}` | `fl_choice`/`fl_alert`/`fl_message` and modal waits routed to the page in mirror mode. Screens call these instead of FLTK directly (many small edits in `*Screen.cpp`). |
| `src/gui_and_draw/MirrorGL.{h,cpp}` | A hidden GL context for incidental GL work; mirrored GL views are drawn by the page. |
| `src/gui_and_draw/NativeFileDialog.{h,cpp}` | Windows Explorer file dialogs for the local engine. |
| `src/gui_and_draw/CrashTrace.{h,cpp}` | Symbolized stack traces in mirror mode. |
| `src/gui_and_draw/MainGLWindow`, `SubGLWindow`, `VSPWindow`, `ScreenMgr`, `ManageGeomScreen`, `TreeIconWidget` | Hooks the mirror needs (camera/view state for GL views, hidden windows, `ManageGeomScreen::Toggle` for multi-select). |
| `src/geom_api/VSP_Geom_API.{h,cpp}`, `GuiInterface.{h,cpp}` | API for the web: `Mirror*` (enable, pump, events, output, pick, native dialog), `MirrorGeomOrigin`, `MirrorMoveGeoms`, `MirrorFeatureLines`, `GetMeshGeomTriPnts`, `GetActiveGeoms`, `GetParmActiveFlag`. |
| `src/geom_core/ParmMgr.{h,cpp}`, `ParmUndo.h` | Undo groups (`BeginUndoGroup`/`EndUndoGroup`): changes in a group are one `UnDo()` step. |
| `src/geom_core/MeshGeom`, `NGonMeshGeom` | Triangles for drawing Mesh / NGon Mesh geoms (`GetDrawTriPnts`). |
| `src/geom_core/ScriptCall.h`, `ScriptMgr.cpp` | AngelScript registrations through calling-convention macros (generic wrappers in WebAssembly). |
| `src/util/WebProcess.{h,cpp}`, `ProcessUtil.cpp` | Under Emscripten, external programs (vspaero, vsploads) are run by the page in workers. |
| `src/external/sixseries/NACA_6_series.c`, `Airfoil.h` | Signature fixes that trap in WebAssembly. |
| `src/vsp_aero/Solver/VSP_Solver.C` | OpenMP guard (no OpenMP in WebAssembly). |
| `VSP_NO_STEP` (CMake) | Builds without STEPcode (WebAssembly). |

---

## 3. How the GUI mirror works

The core idea: **never reimplement a screen.** If something in an OpenVSP window looks or
behaves wrong in the page, fix how it is mirrored (WebMirror.cpp / `mirror/*.tsx`), not the
screen.

### Engine → page

`WebMirror::Pump(timeout)` runs on the GUI thread: it applies queued client events, runs the
FLTK loop briefly, then `Diff()` walks every shown top-level window and emits messages:

- `{"t":"diff", full, windows, closed, widgets, removed}`: windows (`role` = `main`, `browser`
  for the Geom Browser, `geom` for geom editors, `screen` for other screens, `dialog`; plus
  designed size `dw/dh`) and widgets (id, parent, kind, rect, label, colours, value, state,
  bound parm, kind-specific fields like menu items, tree items, browser lines, table cells,
  GL draw objects, SVG/pixels for plots). Only changes are sent unless `full`.
- `{"t":"dialog", seq, kind, ...}`: a prompt (`choice`, `alert`, `message`, `file`). Choices
  and file choosers block the GUI thread (still pumping) until the page answers.
- `{"t":"overlay", objs}`: what the main 3D view shows besides the model (§5). At most every
  200 ms, only when changed.

The local engine wraps them as `{"type":"mirror","msgs":[...]}` on the WebSocket
(`mirror.py`); the in-browser engine returns them from `take()`. New clients get a snapshot
(`RequestFull`).

### Page → engine

`sendMirror(event)` (`mirror/store.ts`) posts flat JSON events, applied by
`ApplyEvent()` in WebMirror.cpp. Ops: `set` (value + callback), `click`, `check`, `pick`
(menu item), `tab`, `browser`, `tree_icon`, `text`, `key`, `mouse` (replayed mouse events,
for trees/canvases/GL views), `resize`, `close`, `dialog` (prompt answers), `full`.

### Page side

`mirror/store.ts` holds windows and widgets by id and children by `win:parent`.
`Widgets.tsx` draws each widget at its exact FLTK position with a component per `kind`.
`Window.tsx` draws floating windows; `Dock.tsx` the side panels. The left panel shows the
`browser` window and the right one the first docked editor (`isDocked` in `App.tsx`: `geom`
windows except Group Modifications, and the Vehicle screen). Resizing a panel resizes the real
FLTK window so it re-lays out.

---

## 4. Data flow for model changes

1. The page calls an operation (`api.setParm`, `api.addGeom`, `api.moveGeoms`, ...) or the
   user edits through a mirrored widget.
2. OpenVSP updates.
3. Change detection emits `{"type":"model_changed", rev, tree, changed, removed, ...}`:
   - `_after_change` after an API operation, or
   - the watcher (`session._watch` / `engine.cpp`), which polls `GetAndResetUpdateCount()`
     and a tree signature, and fingerprints each geom's parm values (a child includes its
     parent's fingerprint, since it moves with it).
4. `store.handleEvent` bumps `meshRev[id]` for changed geoms and reloads the tree if needed.
5. `GeomMesh` refetches `GET /geoms/{id}/mesh` when its `meshRev` changes.

Selection is OpenVSP's: the tree's `active` list comes from `GetActiveGeoms()`. Clicking in
the 3D view calls `api.pick(id, toggle)` → `MirrorPickGeom` → `ManageGeomScreen::Set` /
`Toggle`, so the Geom Browser and editors follow.

### Mesh payload (`VSPM`)

Little-endian, 4-byte aligned. Written by `tess.geom_mesh_bytes` (local) and `MeshBytes`
(`engine.cpp`); read by `api/mesh.ts`:

```
"VSPM", uint32 nsurf
per surface: uint32 nu, nw, nidx; float32 pos[nv*3]; float32 nrm[nv*3]; uint32 idx[nidx]
             (nv = nu*nw; nw = 0 means a plain triangle list of nu vertices: Mesh Geoms)
uint32 nfeat; float32 feat[nfeat*3]        outline segments (pairs), MirrorFeatureLines
```

**Both writers and the reader must change together**, and `web/backend/tests/test_backend.py`
(`parse_mesh`) checks the exact length.

---

## 5. The 3D view (`viewport/`)

- **Surfaces**: per geom, shaded mesh + u/w grid lines + outline (drei `Line`). Draw type per
  geom from the Geom Browser (wire / hidden / shade / none).
- **Overlay**: `MainOverlayJson()` in WebMirror.cpp sends draw objects the page doesn't get
  from meshes:
  - the screens' own draw objects (main GL window's objects minus the vehicle's), such as the
    Mass Properties CG;
  - from the vehicle's geoms: anything whose `m_GeomID` starts with `BBOXHEADER` (selection
    and wing-section boxes, tagged `box: 1|2`) or `XSECHEADER` (cross sections, skinning and
    blending vectors), hinge geoms whole, and **blank geoms as `{"t":"blank", id, sel, p}`
    markers** (drawn as clickable HTML circles).
  Visibility follows OpenVSP's own `m_Visible`, so the page shows what the desktop shows.
  Overlay items are drawn on top (no depth test).
- **Move gizmo** (`MoveGizmo`): drei `TransformControls` on an empty object at the mean of the
  selected origins (`api.geomOrigin`). Dragging only offsets the drawn meshes
  (`useMovePreview`); on release `api.moveGeoms(ids, delta)` → `MirrorMoveGeoms`, which
  converts the world delta into each geom's abs/rel location frame, skips geoms that follow a
  selected ancestor, and sets the parms inside one undo group. `keepArrowsOutward` patches the
  gizmo so arrows always point +X/+Y/+Z and thickens its axis lines.
- **Clicks**: a click on a mesh picks it; Shift/Ctrl/Cmd toggles; a click on nothing (not a
  drag, not on the gizmo) deselects.
- **Camera**: `camera.tsx`, shared with the results viewer. View requests wait until the
  orbit controls are bound to the current camera; a new camera only inherits the pose of one
  that was framed.

---

## 6. VSPAERO

- **Local engine**: OpenVSP runs the native `vspaero`, `vsploads` and `vspviewer` beside it
  (built by `build_vsp.ps1`, packaged in the zip).
- **In-browser engine**: `vspaero`/`vsploads` are WebAssembly programs. `ProcessUtil` (under
  `__EMSCRIPTEN__`) asks the page through `WebProcess`; `wasmEngine.ts` `runProgram` starts a
  `program.worker.js` per run, copies working files in, streams output back
  (`processOutput`), copies written files back and reports the exit (`processExit`). The engine
  creates placeholder files `/bin/vspaero` etc. and calls `SetVSPAEROPath("/bin")` so OpenVSP
  finds them. Single-threaded (no OpenMP).
- **Results viewer**: `vspviewer` requests are intercepted by `openViewer` in
  `wasmEngine.ts`, which reads `<base>.adb`, `.adb.cases`, `.polar` and opens
  `viewer/ResultsViewer.tsx`. The `.adb` layout is checked against the writers in
  `src/vsp_aero/Solver/VSP_Solver.C` (magic `-123789456`, +3 for v3).

---

## 7. Building, running, testing

Windows. Paths below are relative to the repo root unless noted.

| Task | Command |
|---|---|
| First build (libraries, OpenVSP modules, venv, UI) | `web\scripts\build_vsp.ps1` |
| Rebuild the GUI-enabled Python module after C++ changes | `cmake --build build/vsp-gui --config Release --target pyvsp_g`, then copy `build\vsp-gui\python_api\Release\_vsp_g.pyd` into `build\vsp-gui\python_pseudo\openvsp\openvsp\` |
| Run the local engine | `web\vsp-web.cmd` (or `web\.venv\Scripts\vsp-web --no-browser`) → http://localhost:8765 |
| Page dev server | `cd web\frontend; npm run dev` → http://localhost:5173 (in-browser engine; `/local/` for the local one) |
| Build the in-browser engine | `web\wasm\build_wasm.ps1 -Stage all` once; after C++ changes `-Stage vsp -Targets vsp_wasm` (add `vspaero,vsploads` if the solver changed) |
| Type-check the page | `cd web\frontend; npx tsc -b` |
| Backend tests | `web\.venv\Scripts\python -m pytest -q web\backend\tests` |

Toolchain on the maintainer's machine: emsdk at `..\emsdk`, ninja at `..\tools\ninja\bin`,
Node at `C:\Program Files\nodejs`.

**After any C++ change, rebuild both engines** (the wasm engine and `pyvsp_g` + copy), and
hard-refresh the page (the wasm module is cached).

### Releasing

- Push to WebVSP `main` (`git push webvsp wasm-dev:main`) → `pages.yml` deploys the page and
  `engine.yml` builds the local engine zip.
- The **in-browser engine is not built by CI**. Upload a local build to the `wasm-latest`
  release before (or right after) pushing page changes that need it:
  `gh release upload wasm-latest web/frontend/public/wasm/* -R MohammadAdib/WebVSP --clobber`.
  `pages.yml` downloads `vsp*.js` and `vsp*.wasm` from it. If the page and the engine don't
  match, new features quietly fail on the hosted site, so re-run the Pages workflow after
  uploading if it already deployed.

---

## 8. Conventions and traps

- **Repos and branches.** Work on `wasm-dev` (local `web-gui` is kept equal). The public repo is
  the `webvsp` remote (`MohammadAdib/WebVSP`). `origin` is upstream OpenVSP: **never push to
  it**. Don't touch the separate `MohammadAdib/OpenVSP` fork.
- **Commits**: only when asked; no AI co-author or "Generated with" lines.
- **Threading.** All OpenVSP calls happen on one thread: `runtime.py`'s thread (use
  `VspWorker.call`) or the engine pthread in WebAssembly. WebMirror's `PostEvent` and
  `TakeOutput` are the only thread-safe entry points.
- **Two engines, one contract.** A new operation needs: the API function (C++), a route in
  `app.py` + a method in `session.py`, an `op` in `engine.cpp` `Run()` (and its header
  comment), a route in `wasmRequest` (`wasmEngine.ts`), and `api.*` in `client.ts`. The
  in-browser engine only implements the operations the page actually uses.
- **New C++ API functions** go in `VSP_Geom_API.{h,cpp}` with a doc comment: SWIG wraps them
  for Python automatically (the GUI module regenerates `vsp_g.py`). Guard calls in the backend
  with `hasattr(vsp, ...)` when older modules may lack them.
- **Undo.** The page's undo is OpenVSP's parm undo (Edit > Undo). Parms set with `Set()` are not
  recorded; use `SetFromDevice()` like the GUI does, inside `BeginUndoGroup`/`EndUndoGroup`
  when one user action changes several parms.
- **Security.** The local engine refuses requests whose Host/Origin isn't itself or a trusted
  hosted origin (`WEB_ORIGINS` in `app.py`, plus `WEBVSP_WEB_ORIGINS`). An allowed site can
  read and write the user's files: never add origins you don't control.
- **zustand selectors** must return stable values: use shared empty constants (`NO_GEOMS`,
  `NO_IDS`) instead of `?? []`, or the component re-renders forever.
- **Line endings.** Some files are CRLF and some LF. Edit tools handle it; scripted edits must
  preserve the file's existing endings.
- **Shell.** Bash heredocs mangle backslashes: write files containing `\t`, `\n`, `\\` with an
  editor tool, not a heredoc.
- **WebGL lines are 1 px.** For thicker lines use drei `Line` (Line2) or meshes.
- **Tests that parse protocol output** (`test_backend.parse_mesh`, `test_mirror.MirrorClient`)
  must be updated with format changes.

## 9. Not supported yet

- In the browser: STEP export, multithreaded VSPAERO, CalculiX, the desktop VSPAERO viewer
  (the page's viewer replaces it).
- The page's results viewer: control surfaces, rotors, cut planes, coarse mesh levels, surface
  selection, export.
