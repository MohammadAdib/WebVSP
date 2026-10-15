# WebVSP changes

What WebVSP adds on top of OpenVSP, newest first. WebVSP is based on **OpenVSP 3.55.0**
(commit `5240ef210`); OpenVSP's own history is in [CHANGELOG.md](CHANGELOG.md).

## 2026-10-15

### 3D view: moving, selecting, highlighting
- **Move gizmo.** Blender-style X/Y/Z arrows on the selection, at the middle of the selected
  geoms' origins. Dragging moves the geoms along that axis, snapped to a 1-unit grid. While
  dragging only the drawing moves; the move is applied when the arrow is let go, by setting
  each geom's XForm location (absolute or relative, whichever it uses, converted into the
  frame it's attached to). A geom attached to a selected parent moves with the parent.
- **One undo per move.** OpenVSP's parm undo stack gained undo groups
  (`ParmMgr::BeginUndoGroup` / `EndUndoGroup`), so a move of several geoms on several axes is a
  single step of Edit > Undo.
- **Multi-select.** Shift-click or Ctrl-click (Cmd on a Mac) adds a geom to the selection or
  takes it out. A plain click on nothing deselects everything.
- **Group Modifications** opens as a floating window instead of replacing the right panel.
- **Selection highlights like the desktop's:** the selected geom's bounding box (red), the active
  wing section's box (green), the cross sections with the active one in blue, and the skinning
  and blending tangent vectors (dashed where the desktop stipples them, with arrowheads). The
  boxes' opacity is a constant (`BBOX_ALPHA`, `SECTION_BOX_ALPHA`).
- **Outlines.** Each geom draws OpenVSP's feature lines (the desktop's black outline), sent with
  its mesh.
- **Blank geoms** show as circles at their origins: gray, red when selected, clickable.
- **Overlay of the screens' 3D drawings**: Mass Properties' CG (red point), and anything else a
  screen adds to the main view, mirrored from OpenVSP.
- **NGon meshes** (e.g. from Mesh Geom conversion) are drawn.

### VSPAERO
- **Results viewer in the page.** VSPAERO > Launch Viewer opens a floating viewer for the
  `.adb` results: Cp, steady/unsteady Cp, vorticity or shaded, per solution case, with wakes,
  a desktop-style colour legend, the case's coefficients from the `.polar` file and the
  reference values. (The in-browser engine; the local engine runs the desktop viewer.)
- **VSPAERO in the in-browser engine.** `vspaero` and `vsploads` are compiled to WebAssembly
  and run in workers, one fresh instance per run (single-threaded).
- File choosers accept several files at once (a model together with its results).

### Engines
- **The address chooses the engine:** the hosted page at `/` runs OpenVSP in the browser tab;
  `/local/` uses the engine on this computer. Nothing is remembered. The status bar links to
  the other one.
- **In-browser engine.** OpenVSP and its FLTK GUI compiled to WebAssembly: a headless FLTK
  platform, OpenVSP on its own thread, a service worker for cross-origin isolation.
- Portability fixes so the OpenVSP core builds for WebAssembly (AngelScript calling
  conventions, NACA 6-series signatures, `VSP_NO_STEP`).
- **File > New Window** starts another engine, so each window has its own model.
- Native Windows file dialogs for the local engine, remembering the last folder.
- Mesh Geoms are drawn in the 3D view.

## 2026-10-14

- **First version.** A browser GUI for OpenVSP: a local Python engine (FastAPI) runs OpenVSP
  with its real FLTK GUI off screen and mirrors every window, widget, menu and prompt to a
  React page, in a dark theme next to a three.js 3D view. Input goes back to the real widgets,
  so every screen's own logic runs unchanged.
- Hosted on GitHub Pages, talking to a local engine; a portable Windows engine zip built by
  GitHub Actions.
- 3D view toolbar (front, side, top, iso, fit), orthographic/perspective switch, Blender
  numpad keys.
- VSPAERO's solver consoles and result plots mirrored.
- Resizable side panels that re-lay out the real FLTK windows.
- Renamed from TitanVSP to WebVSP.
