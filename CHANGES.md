# WebVSP changes

Everything WebVSP changes or adds on top of **OpenVSP 3.55.0** (commit `5240ef210`). OpenVSP's own
history is in [CHANGELOG.md](CHANGELOG.md).

- OpenVSP, its FLTK GUI and VSPAERO compiled to WebAssembly and run entirely in the browser tab;
  nothing to install.
- OpenVSP's real FLTK GUI kept off screen and mirrored, window by window and widget by widget, into
  a React page in a dark theme; input goes back to the real widgets, so every screen's own logic
  runs unchanged.
- A headless FLTK platform: windows lay out but are never displayed, the event loop never blocks,
  text is measured with the browser's canvas.
- OpenVSP runs on its own thread; a service worker makes the page cross-origin isolated for it.
- Prompts (`fl_choice`, `fl_alert`, `fl_message`) shown as page dialogs.
- File choosers use the browser's file pickers, and accept several files at once (a model together
  with its results).
- Every model you open or save is linked to the file on your disk: File > Save (and Ctrl+S) writes
  it back in place in Chrome and Edge (which ask once for permission to edit the file), and
  downloads it in other browsers.
- Plot widgets captured as SVG.
- VSPAERO's solver consoles and result plots mirrored.
- Hosted on GitHub Pages; a push to `main` builds the engine and the page in CI and deploys them,
  with nothing built or uploaded by hand.
- The Geom Browser docked on the left and the geom editor on the right; other screens and popups
  are floating windows.
- Resizable side panels that re-lay out the real FLTK windows; the 3D view follows them smoothly.
- Resizable floating windows (VSPAERO's among them), re-laid out like the desktop's, never below
  their designed size.
- Group Modifications opens as a floating window instead of replacing the right panel.
- FLTK symbol labels drawn with the right direction, size and flips (e.g. the Geom Browser's move
  up/down buttons).
- A three.js 3D view drawing each geom from the GUI's own u/w tessellation, following each geom's
  draw type (wire, hidden, shade, none) and the GUI's selection.
- 3D view toolbar (front, side, top, bottom, iso, fit), orthographic/perspective switch keeping
  the view, Blender numpad keys, orbit, pan and zoom to the cursor.
- Moving to a view is animated (keys, F5-F12, toolbar, orbit steps, the orientation gizmo's
  balls), turning in elevation and compass direction so top and bottom views arrive the right
  way round. The side view (3) has the nose on the left, as in Blender.
- Blender's auto perspective: axis views (front, side, top, bottom, rear) switch to
  orthographic, and turning away (orbit keys, Iso, the mouse, as soon as it starts turning)
  goes back to perspective if it was perspective before; 5 chooses by hand.
- Disks (propellers in disk mode) are drawn see-through (25%) when shaded.
- No footer: the OpenVSP version is at the right of the top bar (with any error), and the model's
  file name at the bottom left of the 3D view.
- Split views like desktop OpenVSP's Window menu: one, two side by side, two stacked or four
  panes (toolbar icons), each with its own camera; the toolbar, keys and View menu act on the
  active pane, and a newly loaded model is framed in every pane. Changing the layout fades the
  view out and in, taking no input meanwhile.
- A Blender-style ground grid: lines at round spacings that follow the zoom, red X and green Y
  axes, a square under the model that follows it and fades out at 150% of its footprint, and a
  toolbar switch to hide it.
- File > Preferences (only a choice of file chooser) left out of the menu: files always go
  through the browser's.
- Outlines: each geom draws OpenVSP's feature lines (the desktop's black outline).
- Mesh and NGon Mesh geoms (CompGeom results, imported STL/TRI, NGon conversions, VSPAERO's
  geometry) are drawn, per tag in the desktop's colour-wheel colours, NGon meshes with their
  polygon edges.
- Selection highlights like the desktop's: the selected geom's bounding box (red), the active wing
  section's box (green), the cross sections with the active one in blue, and the skinning and
  blending tangent vectors (dashed where the desktop stipples them, with arrowheads). The boxes'
  opacity is a constant (`BBOX_ALPHA`, `SECTION_BOX_ALPHA`).
- The screens' drawings in the main view mirrored: Mass Properties' CG (red point), and anything
  else a screen adds.
- Blank geoms shown as circles at their origins: gray, red when selected, clickable.
- Click to select a geom in the 3D view; Shift-click or Ctrl-click (Cmd on a Mac) adds it to the
  selection or takes it out; a click on nothing deselects everything.
- Move gizmo: Blender-style X/Y/Z arrows on the selection, at the middle of the selected geoms'
  origins. Dragging moves the geoms along that axis, snapped to a 1-unit grid, by setting each
  geom's XForm location (absolute or relative, converted into the frame it's attached to); a geom
  attached to a selected parent moves with the parent. The move is applied when the arrow is let
  go.
- Undo groups in OpenVSP's parm undo stack (`ParmMgr::BeginUndoGroup` / `EndUndoGroup`), so a
  move of several geoms on several axes is a single step of Edit > Undo.
- VSPAERO runs in the browser: `vspaero` and `vsploads` compiled to WebAssembly and run in workers,
  one fresh instance per run (single-threaded).
- VSPAERO results viewer in the page (VSPAERO > Launch Viewer), in a movable, resizable window:
  Cp, steady/unsteady Cp, vorticity or shaded, per solution case, with wakes and wake surfaces, a
  desktop-style colour legend filling the window's height, smooth shading on by default, the
  case's coefficients from the `.polar` file and the reference values.
- The viewer's wakes drawn twice as long, the added half fading out, instead of the desktop
  viewer's infinite wake option.
- Portability fixes so the OpenVSP core builds for WebAssembly: AngelScript calling conventions,
  NACA 6-series signatures, an OpenMP guard in VSPAERO, building without STEPcode
  (`VSP_NO_STEP`).
