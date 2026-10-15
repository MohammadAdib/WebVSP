//
// Definitions for the "headless" platform: FLTK without a window system, as used by
// OpenVSP's in-browser engine (WebAssembly). Windows are never displayed; their widget trees
// are mirrored to a web page, which also supplies the input (see OpenVSP's WebMirror).
//
// Part of WebVSP's FLTK port (web/wasm/fltk), copied into the FLTK source tree at build time.
//
// This library is free software. Distribution and use rights are outlined in
// the file "COPYING" which should have been included with this file.  If this
// file is missing or damaged, see the license at:
//
//     https://www.fltk.org/COPYING.php
//

#if !defined(FL_PLATFORM_H)
#  error "Never use <FL/headless.H> directly; include <FL/platform.H> instead."
#endif // !FL_PLATFORM_H

/** A shown window's platform reference: an opaque id, there being no window system. */
typedef struct fl_headless_window *Window;
