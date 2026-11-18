//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

#ifndef MIRRORGL_H_
#define MIRRORGL_H_

class Fl_Window;

// Create a hidden GL context and initialize GLEW (web-mirrored GUI only).
void MirrorCreateGLContext();

// Make that context current again (cheap; call before running GUI code).
void MirrorMakeGLCurrent();

// The hidden window that owns the context (not mirrored to the client).
Fl_Window *MirrorGLWindow();

#endif // MIRRORGL_H_
