//
// Driver classes of the "headless" platform: FLTK without a window system (see FL/headless.H).
//
// Windows are shown and hidden logically (shown(), Fl::first_window(), modality and layout all
// behave as usual) but nothing is ever drawn on screen. The event loop never blocks: the
// application pumps it, and supplies input by calling handle() and widget callbacks itself.
// Text is measured with the browser's canvas when built with Emscripten.
//
// Part of WebVSP's FLTK port (web/wasm/fltk), copied into the FLTK source tree at build time.
//
// This library is free software. Distribution and use rights are outlined in
// the file "COPYING" which should have been included with this file.  If this
// file is missing or damaged, see the license at:
//
//     https://www.fltk.org/COPYING.php
//

#ifndef FL_HEADLESS_DRIVERS_H
#define FL_HEADLESS_DRIVERS_H

#include <config.h>
#include "../Posix/Fl_Posix_System_Driver.H"
#include "../../Fl_Screen_Driver.H"
#include "../../Fl_Window_Driver.H"
#include <FL/Fl_Graphics_Driver.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_Copy_Surface.H>

class Fl_Headless_System_Driver : public Fl_Posix_System_Driver {
public:
  double wait(double time_to_wait) FL_OVERRIDE;
  int ready() FL_OVERRIDE;
  // Awake handlers are run by wait(); there is no pipe to wake a blocked loop.
  void awake(void *) FL_OVERRIDE {}
  int filename_list(const char *d, dirent ***list,
                    int (*sort)(struct dirent **, struct dirent **),
                    char *errmsg = NULL, int errmsg_sz = 0) FL_OVERRIDE;
  char *preference_rootnode(Fl_Preferences *prefs, Fl_Preferences::Root root,
                            const char *vendor, const char *application) FL_OVERRIDE;
  void newUUID(char *uuidBuffer) FL_OVERRIDE;
  int open_uri(const char *, char *msg, int msglen) FL_OVERRIDE;
};

class Fl_Headless_Screen_Driver : public Fl_Screen_Driver {
public:
  void init() FL_OVERRIDE { num_screens = 1; }
  void open_display_platform() FL_OVERRIDE;
  void screen_xywh(int &X, int &Y, int &W, int &H, int n) FL_OVERRIDE;
  void screen_work_area(int &X, int &Y, int &W, int &H, int n) FL_OVERRIDE;
  void screen_dpi(float &h, float &v, int n = 0) FL_OVERRIDE { h = v = 96; (void)n; }
  int get_mouse(int &x, int &y) FL_OVERRIDE;
  int event_key(int k) FL_OVERRIDE;
  int get_key(int k) FL_OVERRIDE { return event_key(k); }
  int compose(int &del) FL_OVERRIDE;
  void copy(const char *stuff, int len, int clipboard, const char *type) FL_OVERRIDE;
  void paste(Fl_Widget &receiver, int clipboard, const char *type) FL_OVERRIDE;
  int clipboard_contains(const char *type) FL_OVERRIDE;
};

class Fl_Headless_Window_Driver : public Fl_Window_Driver {
public:
  Fl_Headless_Window_Driver(Fl_Window *w) : Fl_Window_Driver(w) {}
  void show() FL_OVERRIDE;
  void hide() FL_OVERRIDE;
  void resize(int X, int Y, int W, int H) FL_OVERRIDE;
  // Never drawn: windows only exist to be mirrored.
  void flush() FL_OVERRIDE {}
  void make_current() FL_OVERRIDE {}
};

class Fl_Headless_Graphics_Driver : public Fl_Graphics_Driver {
public:
  void font(Fl_Font face, Fl_Fontsize fsize) FL_OVERRIDE;
  Fl_Font font() FL_OVERRIDE { return Fl_Graphics_Driver::font(); }
  double width(const char *str, int n) FL_OVERRIDE;
  double width(unsigned int c) FL_OVERRIDE;
  int height() FL_OVERRIDE;
  int descent() FL_OVERRIDE;
  const char *font_name(int num) FL_OVERRIDE;
  void font_name(int num, const char *name) FL_OVERRIDE;
  const char *get_font_name(Fl_Font fnum, int *ap) FL_OVERRIDE;
  Fl_Font set_fonts(const char *) FL_OVERRIDE { return FL_FREE_FONT; }
};

class Fl_Headless_Image_Surface_Driver : public Fl_Image_Surface_Driver {
public:
  Fl_Headless_Image_Surface_Driver(int w, int h, int high_res, Fl_Offscreen off);
  ~Fl_Headless_Image_Surface_Driver();
  void set_current() FL_OVERRIDE;
  void translate(int, int) FL_OVERRIDE {}
  void untranslate() FL_OVERRIDE {}
  // Nothing is rasterized: a blank (white) image.
  Fl_RGB_Image *image() FL_OVERRIDE;
};

class Fl_Headless_Copy_Surface_Driver : public Fl_Copy_Surface_Driver {
public:
  Fl_Headless_Copy_Surface_Driver(int w, int h);
  ~Fl_Headless_Copy_Surface_Driver();
  void set_current() FL_OVERRIDE;
  void translate(int, int) FL_OVERRIDE {}
  void untranslate() FL_OVERRIDE {}
};

#endif // FL_HEADLESS_DRIVERS_H
