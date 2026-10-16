# WebVSP changes

Everything WebVSP changes or adds on top of **OpenVSP 3.55.0** (commit `5240ef210`). OpenVSP's own
history is in [CHANGELOG.md](CHANGELOG.md).

## In the browser

- OpenVSP, its FLTK GUI and VSPAERO compiled to WebAssembly and run entirely in the browser tab;
  nothing to install. OpenVSP runs on its own thread; a service worker makes the page
  cross-origin isolated for it.
- Hosted on GitHub Pages; a push to `main` builds the engine and the page in CI and deploys them,
  with nothing built or uploaded by hand.
- The WebVSP icon as the favicon, and installable as an app (a web app manifest).

## OpenVSP's GUI, mirrored

- OpenVSP's real FLTK GUI kept off screen and mirrored, window by window and widget by widget, into
  a React page in a dark theme; input goes back to the real widgets, so every screen's own logic
  runs unchanged.
- A headless FLTK platform: windows lay out but are never displayed, the event loop never blocks,
  text is measured with the browser's canvas.
- The Geom Browser docked on the left and the geom editor on the right, in resizable side panels
  that re-lay out the real FLTK windows (the 3D view follows them smoothly). Other screens,
  Group Modifications among them, and popups are floating windows, resizable like the desktop's
  and never below their designed size.
- The top bar: OpenVSP's menus, then any error, the OpenVSP version and a link to WebVSP's GitHub
  repository. The model's file name is at the bottom left of the 3D view.
- Prompts (`fl_choice`, `fl_alert`, `fl_message`) shown as page dialogs.
- Plot widgets captured as SVG; VSPAERO's solver consoles and result plots mirrored.
- FLTK symbol labels drawn with the right direction, size and flips (e.g. the Geom Browser's move
  up/down buttons).

## Files

- File choosers use the browser's file pickers, and accept several files at once (a model together
  with its results). File > Preferences (only a choice of file chooser) is left out of the menu.
- Every model you open or save is linked to the file on your disk: File > Save (and Ctrl+S) writes
  it back in place in Chrome and Edge (which ask once for permission to edit the file), and
  downloads it in other browsers.

## The 3D view

- A three.js 3D view drawing each geom from the GUI's own u/w tessellation, following each geom's
  draw type (wire, hidden, shade, none) and the GUI's selection. Each geom draws OpenVSP's feature
  lines (the desktop's black outline); disks (propellers in disk mode) are see-through (25%) when
  shaded.
- Mesh and NGon Mesh geoms (CompGeom results, imported STL/TRI, NGon conversions, VSPAERO's
  geometry) are drawn, per tag in the desktop's colour-wheel colours, NGon meshes with their
  polygon edges.
- Selection highlights like the desktop's: the selected geom's bounding box (red), the active wing
  section's box (green), the cross sections with the active one in blue, and the skinning and
  blending tangent vectors (dashed where the desktop stipples them, with arrowheads).
- The screens' drawings in the main view mirrored: Mass Properties' CG (red point), and anything
  else a screen adds. Blank geoms shown as circles at their origins: gray, red when selected,
  clickable.
- A toolbar (front, side, top, bottom, iso, fit), orthographic or perspective, and orbit, pan and
  zoom to the cursor with the mouse.
- Blender's view keys: numpad 1 / 3 / 7 (Ctrl for the other side), 5, 9, and 2 / 4 / 6 / 8 orbiting
  30° per press (8 and 2 stopping at the top and bottom views); F (or Home, numpad .) fits the
  model; C (View > Center) centers it, keeping the zoom. The side view (3) has the nose on the
  left, as in Blender.
- Moving to a view is animated (keys, F5-F12, toolbar, orbit steps, the orientation gizmo's
  balls), turning in elevation and compass direction so top and bottom views arrive the right
  way round.
- Blender's auto perspective: axis views (front, side, top, bottom, rear) switch to
  orthographic, and turning away (orbit keys, Iso, the mouse, as soon as it starts turning)
  goes back to perspective if it was perspective before; 5 chooses by hand.
- Split views like desktop OpenVSP's Window menu: one, two side by side, two stacked or four
  panes (toolbar icons), each with its own camera; the toolbar, keys and View menu act on the
  active pane, and a newly loaded model is framed in every pane. Changing the layout fades the
  view out and in, taking no input meanwhile.
- A Blender-style ground grid: lines at round spacings that follow the zoom, red X and green Y
  axes, a square under the whole model (hidden geoms included) that fades out at 150% of its
  footprint, and a toolbar switch to hide it.
- A keyboard button beside the toolbar lists the 3D view's keys.

## Selecting and editing

- Click to select a geom in the 3D view; Shift-click or Ctrl-click (Cmd on a Mac) adds it to the
  selection or takes it out; a click on nothing deselects everything.
- Blender's select and hide keys, acting on every selected geom: A selects all, Alt+A deselects
  all, H hides the selection, Shift+H hides everything else, Alt+H shows everything (the geoms
  it reveals join the selection).
- Z switches the selection between wireframe and shaded; Shift+Z does it for every geom
  (shaded if every shown geom is wireframe, otherwise wireframe).
- Move gizmo: Blender-style X/Y/Z arrows on the selection, at the middle of the selected geoms'
  origins. Dragging moves the geoms along that axis, snapped to a 1-unit grid, by setting each
  geom's XForm location (absolute or relative, converted into the frame it's attached to); a geom
  attached to a selected parent moves with the parent. The move is applied when the arrow is let
  go.
- Blender's G and R: G moves the selection and R rotates it, each geom about its own origin. The
  mouse drives it; X, Y or Z holds it to that axis (drawn through the selection in the axis's
  colour); typing a number gives the exact distance or angle (G X 10 moves 10 along X, R Z 90
  turns 90° about Z); Ctrl snaps. Enter or a click applies it as one Edit > Undo step; Esc or a
  right click cancels.
- Shift+D duplicates the selection in place (each copy under its original's parent, or under
  the copy of its parent when that's duplicated too) and starts G on the copies; Esc leaves
  them where they are.
- X or Delete deletes the selection after asking (Enter or Delete to confirm, Esc to keep it);
  OpenVSP's own question about clones follows when there are any.
- Undo groups in OpenVSP's parm undo stack (`ParmMgr::BeginUndoGroup` / `EndUndoGroup`), so a
  move or rotation of several geoms is a single step of Edit > Undo.

## VSPAERO

- VSPAERO runs in the browser: `vspaero` and `vsploads` compiled to WebAssembly and run in workers,
  one fresh instance per run (single-threaded).
- VSPAERO results viewer in the page (VSPAERO > Launch Viewer), in a movable, resizable window:
  Cp, steady/unsteady Cp, vorticity or shaded, per solution case, with wakes and wake surfaces, a
  desktop-style colour legend filling the window's height, smooth shading on by default, the
  case's coefficients from the `.polar` file and the reference values.
- The viewer's wakes drawn twice as long, the added half fading out, in place of the desktop
  viewer's infinite wake option.

## Building for WebAssembly

- Portability fixes so the OpenVSP core builds for WebAssembly: AngelScript calling conventions,
  NACA 6-series signatures, an OpenMP guard in VSPAERO, building without STEPcode
  (`VSP_NO_STEP`).
