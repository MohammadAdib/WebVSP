//
// OpenGL windows on the "headless" platform (see FL/headless.H): they can be created, shown
// and laid out like any window, but have no GL context and are never drawn.
//
// Part of WebVSP's FLTK port (web/wasm/fltk), copied into the FLTK source tree at build time.
//
// This library is free software. Distribution and use rights are outlined in
// the file "COPYING" which should have been included with this file.  If this
// file is missing or damaged, see the license at:
//
//     https://www.fltk.org/COPYING.php
//

#include <config.h>
#if HAVE_GL
#include <FL/Fl_Gl_Window.H>
#include <FL/Fl_RGB_Image.H>
#include "../../Fl_Gl_Window_Driver.H"
#include "../../Fl_Gl_Choice.H"

#include <string.h>

class Fl_Headless_Gl_Window_Driver : public Fl_Gl_Window_Driver {
public:
  Fl_Headless_Gl_Window_Driver(Fl_Gl_Window *win) : Fl_Gl_Window_Driver(win) {}
  // Any mode is "available", so the window shows (it just has no context).
  Fl_Gl_Choice *find(int m, const int *alistp) FL_OVERRIDE {
    Fl_Gl_Choice *g = find_begin(m, alistp);
    if (g) return g;
    g = new Fl_Gl_Choice(m, alistp, first);
    first = g;
    return g;
  }
  void *GetProcAddress(const char *) FL_OVERRIDE { return NULL; }
  Fl_RGB_Image *capture_gl_rectangle(int, int, int w, int h) FL_OVERRIDE {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    uchar *data = new uchar[(size_t)w * h * 3];
    memset(data, 255, (size_t)w * h * 3);
    Fl_RGB_Image *img = new Fl_RGB_Image(data, w, h, 3);
    img->alloc_array = 1;
    return img;
  }
};

Fl_Gl_Window_Driver *Fl_Gl_Window_Driver::newGlWindowDriver(Fl_Gl_Window *w)
{
  return new Fl_Headless_Gl_Window_Driver(w);
}
#endif // HAVE_GL
