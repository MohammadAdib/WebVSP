# WebVSP engine: OpenVSP in WebAssembly

OpenVSP, its FLTK GUI and VSPAERO compiled to WebAssembly, running inside the browser tab.
OpenVSP's own windows, menus and dialogs are mirrored into the page; the 3D view is the page's
own. Nothing is installed and nothing runs outside the browser.

## Build

Needs the Emscripten SDK and Ninja next to the repo (paths are parameters of the scripts), plus
Node LTS for the page:

```powershell
# once: tools (outside the repo)
git clone https://github.com/emscripten-core/emsdk ..\emsdk; ..\emsdk\emsdk.bat install latest; ..\emsdk\emsdk.bat activate latest
python -m pip install ninja --target ..\tools\ninja

web\wasm\build_wasm.ps1    # libraries, FLTK, OpenVSP, VSPAERO -> web\frontend\public\wasm\
web\wasm\serve.ps1 -Open   # builds the page, serves http://localhost:4173/
```

| Option | What it rebuilds |
|---|---|
| `-Stage all` (default) | Everything: third-party libraries, FLTK, OpenVSP and its programs. |
| `-Stage libs` | Third-party libraries only. |
| `-Stage fltk` | FLTK only (with the headless platform). |
| `-Stage vsp` | OpenVSP only: what you need after changing C++ in `src/`. |
| `-Targets vsp_wasm,vspaero,vsploads` | Which programs to build (default all three). `vsp_wasm` is OpenVSP itself. |

The first full build takes a while; later ones are incremental. The output
(`vsp.js/.wasm`, `vspaero.js/.wasm`, `vsploads.js/.wasm`) is gitignored.

During development, `cd web\frontend; npm run dev` serves the page with hot reload at
http://localhost:5173/ using the same output. Hard-refresh after rebuilding the engine.

## How it works

| Piece | Where | What |
|---|---|---|
| Engine | `engine/engine.cpp` | OpenVSP runs `main()` on its own thread (`PROXY_TO_PTHREAD`), pumps the GUI and watches the model for changes. The page queues requests (`call`) and collects results, model events and GUI output (`take`). The request list is in its header comment. |
| Page side | `web/frontend/src/api/wasmEngine.ts` | Loads the module, sends requests, delivers events; GUI file choosers use the browser's file pickers (files live in the module's in-memory file system). |
| Threads | `web/frontend/public/coi-sw.js`, `main.tsx` | Threads need a cross-origin isolated page; static hosts can't send the headers, so a service worker adds them (one automatic reload). |
| FLTK | `fltk/` | A "headless" FLTK platform: windows exist and lay out but are never displayed; the event loop never blocks; text is measured with the browser's canvas. `patch_fltk.py` adds it to FLTK's sources at build time. |
| Plots | `src/gui_and_draw/WebMirror.cpp` | Plot widgets are captured as SVG (FLTK's SVG surface). |
| OpenGL | `engine/gl_stub.c` | The GUI never draws with OpenGL here: do-nothing GL generated from Emscripten's headers (`gen_gl_stub.py`, `gl_symbols.txt`). |
| VSPAERO | `src/util/WebProcess.h`, `web/frontend/public/program.worker.js` | `vspaero` and `vsploads` are WebAssembly programs too; OpenVSP starts them through the page, which runs each in a worker with copies of the working files. Single-threaded. |
| Scripting | `src/geom_core/ScriptCall.h` | AngelScript can't call native functions in WebAssembly; registrations go through generic wrappers. |
| Build | `build_wasm.ps1`, `toolchain.cmake` | Everything is built with `-pthread -fwasm-exceptions`. |

## Not available (yet)

- **STEP export**: STEPcode's build runs a code generator, which can't run when cross-compiling
  (`VSP_NO_STEP`).
- **VSPAERO's desktop viewer** (`vspviewer`, an OpenGL program). VSPAERO > Launch Viewer opens
  the page's own results viewer instead (`web/frontend/src/viewer/`).
- **Multithreaded VSPAERO** (no OpenMP) and **CalculiX** (structural analysis, not part of
  OpenVSP).
- Files are opened and saved through the browser. Chrome and Edge save back to the chosen file
  and remember the folder; other browsers download the saved file.
