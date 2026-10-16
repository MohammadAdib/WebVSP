<table width="100%">
<tr>
<td valign="middle" width="220">

<img width="200" height="200" alt="WebVSP" src="https://github.com/user-attachments/assets/3aa57595-c653-4c14-b44d-802d2e1f70d5" />

</td>
<td valign="middle">

[OpenVSP](https://openvsp.org), NASA's parametric aircraft geometry tool, in a web browser.

**Open it: https://mohammadadib.github.io/WebVSP/**

Nothing to install. OpenVSP and all of its functionality, compiled to WebAssembly, runs in the browser tab on any device that can access the webpage!

</td>
</tr>
</table>

<img width="2880" height="1728" alt="Screenshot 2026-10-15 235821" src="https://github.com/user-attachments/assets/f74faf3c-2825-41e4-8649-8769eedb5f1e" />
<img width="2880" height="1728" alt="Screenshot 2026-10-15 235857" src="https://github.com/user-attachments/assets/3accdc2e-48e7-4e4a-9bf4-4ac7a8e979aa" />
<img width="2880" height="1728" alt="Screenshot 2026-10-15 234700" src="https://github.com/user-attachments/assets/3758dd3d-7011-41f6-9a17-62bcf8d1a093" />
<img width="2880" height="1728" alt="Screenshot 2026-10-15 235703" src="https://github.com/user-attachments/assets/8db805a8-62f6-42b8-b0c4-8d93a876a07b" />
<img width="2880" height="1728" alt="image" src="https://github.com/user-attachments/assets/f67095bf-092b-400b-8d92-4d28aac9df14" />


## What you get

- OpenVSP's screens, menus and analyses, including VSPAERO (single-threaded) with a results
  viewer in the page.
- A 3D view with orbit, pan and zoom to the cursor, a view toolbar, Blender numpad keys and
  orthographic or perspective views.
- Click to select, Shift- or Ctrl-click to select several, click on nothing to deselect.
- Move arrows on the selection, snapped to a 1-unit grid, with one undo per move.
- The desktop's selection boxes, cross sections, outlines, CG and other drawings.
- Files are opened and saved through the browser; in Chrome and Edge, Save overwrites the file.
  Each tab is its own window with its own model.

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
