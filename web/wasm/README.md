# WebVSP in-browser engine (experimental)

OpenVSP compiled to WebAssembly, running inside the browser tab: no engine to download or
run. Open the UI with `?engine=wasm`. Everything else in the UI is the same as with the local
engine: OpenVSP's own windows, menus and dialogs are mirrored into the page, and the 3D view is
the page's own.

## Try it locally

Needs the Emscripten SDK and Ninja next to the repo (paths are parameters of the scripts):

```powershell
# once: tools (outside the repo)
git clone https://github.com/emscripten-core/emsdk ..\emsdk; ..\emsdk\emsdk.bat install latest; ..\emsdk\emsdk.bat activate latest
web\.venv\Scripts\python -m pip install ninja --target ..\tools\ninja

web\wasm\build_wasm.ps1    # third-party libraries, FLTK, OpenVSP -> web\frontend\public\wasm\vsp.{js,wasm}
web\wasm\serve.ps1 -Open   # builds the UI, serves http://localhost:4173/?engine=wasm
```

`build_wasm.ps1 -Stage vsp` rebuilds just OpenVSP (after C++ changes); `-Stage fltk` just FLTK.
The first full build takes a while; later ones are incremental.

## How it works

| Piece | Where | What |
|---|---|---|
| Engine | `engine/engine.cpp` | OpenVSP runs `main()` on its own thread (`PROXY_TO_PTHREAD`), pumps the GUI and watches the model, like the Python engine's runtime/session. The page queues requests (`call`) and collects results, model events and GUI output (`take`). |
| Page side | `web/frontend/src/api/wasmEngine.ts` | Stands in for the HTTP API and WebSocket; GUI file choosers use the browser's file pickers (files live in the module's in-memory file system). |
| Threads | `web/frontend/public/coi-sw.js`, `main.tsx` | Threads need a cross-origin isolated page; static hosts can't send the headers, so a service worker adds them (one automatic reload). |
| FLTK | `fltk/` | A "headless" FLTK platform: windows exist and lay out but are never displayed; event loop never blocks; text measured with the browser's canvas. `patch_fltk.py` adds it to FLTK's sources at build time. |
| Plots | `src/gui_and_draw/WebMirror.cpp` | Plot widgets are captured as SVG (FLTK's SVG surface) instead of pixels. |
| OpenGL | `engine/gl_stub.c` | The GUI never draws with OpenGL here: do-nothing GL generated from Emscripten's headers (`gen_gl_stub.py`). |
| Scripting | `src/geom_core/ScriptCall.h` | AngelScript can't call native functions in WebAssembly; registrations go through generic wrappers (natively they're unchanged). |
| Build | `build_wasm.ps1`, `toolchain.cmake` | Everything is built with `-pthread -fwasm-exceptions`. |

## Not available in the browser (yet)

- **VSPAERO** and other external solvers: they're separate programs, which a page can't run
  (the VSPAERO screen opens, but Launch Solver stays disabled).
- **STEP export**: STEPcode's build runs a code generator, which can't run when
  cross-compiling (`VSP_NO_STEP`).
- Files: opened and saved through the browser. Chrome and Edge save back to the chosen file
  and remember the folder; other browsers download the saved file.
