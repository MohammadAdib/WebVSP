//
// The "headless" platform: FLTK without a window system (see FL/headless.H and
// Fl_Headless_Drivers.H). Defines the platform's driver factories and global symbols, and
// the drivers themselves.
//
// Part of WebVSP's FLTK port (web/wasm/fltk), copied into the FLTK source tree at build time.
//
// This library is free software. Distribution and use rights are outlined in
// the file "COPYING" which should have been included with this file.  If this
// file is missing or damaged, see the license at:
//
//     https://www.fltk.org/COPYING.php
//

#include "Fl_Headless_Drivers.H"

#include <FL/Fl.H>
#include <FL/platform.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Device.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Preferences.H>
#include <FL/fl_string_functions.h>

#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <map>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

//==== Factories ====//

Fl_System_Driver *Fl_System_Driver::newSystemDriver() { return new Fl_Headless_System_Driver(); }

Fl_Screen_Driver *Fl_Screen_Driver::newScreenDriver() { return new Fl_Headless_Screen_Driver(); }

Fl_Window_Driver *Fl_Window_Driver::newWindowDriver(Fl_Window *w) { return new Fl_Headless_Window_Driver(w); }

Fl_Graphics_Driver *Fl_Graphics_Driver::newMainGraphicsDriver() { return new Fl_Headless_Graphics_Driver(); }

Fl_Image_Surface_Driver *Fl_Image_Surface_Driver::newImageSurfaceDriver(int w, int h, int high_res, Fl_Offscreen off)
{
  return new Fl_Headless_Image_Surface_Driver(w, h, high_res, off);
}

Fl_Copy_Surface_Driver *Fl_Copy_Surface_Driver::newCopySurfaceDriver(int w, int h)
{
  return new Fl_Headless_Copy_Surface_Driver(w, h);
}

// FLTK's own file chooser (applications usually intercept file dialogs on this platform).
Fl_Native_File_Chooser::Fl_Native_File_Chooser(int val)
{
  platform_fnfc = new Fl_Native_File_Chooser_FLTK_Driver(val);
}

//==== Globals the common code expects a platform to define ====//

Window fl_window = 0;

static void nothing() {}
void (*fl_lock_function)() = nothing;
void (*fl_unlock_function)() = nothing;

// Font table: the 16 standard FLTK faces (names only; text is measured in width() below).
static Fl_Fontdesc built_in_table[] = {
  {"Helvetica"}, {"Helvetica Bold"}, {"Helvetica Italic"}, {"Helvetica Bold Italic"},
  {"Courier"}, {"Courier Bold"}, {"Courier Italic"}, {"Courier Bold Italic"},
  {"Times"}, {"Times Bold"}, {"Times Italic"}, {"Times Bold Italic"},
  {"Symbol"}, {"Screen"}, {"Screen Bold"}, {"Dingbats"},
};
FL_EXPORT Fl_Fontdesc *fl_fonts = built_in_table;

//==== System: a non-blocking event loop ====//

static void run_awake_handlers()
{
  Fl_Awake_Handler func;
  void *data;
  while (Fl::get_awake_handler_(func, data) == 0) func(data);
}

double Fl_Headless_System_Driver::wait(double time_to_wait)
{
  // Nothing ever blocks here: the application pumps the loop (and supplies the input).
  Fl::flush();
  run_awake_handlers();
  double t = Fl_System_Driver::wait(time_to_wait); // widget deletion, timeouts, checks, idle
  run_awake_handlers();
  return t;
}

int Fl_Headless_System_Driver::ready()
{
  return !awake_ring_empty();
}

int Fl_Headless_System_Driver::filename_list(const char *d, dirent ***list,
                                             int (*sort)(struct dirent **, struct dirent **),
                                             char *errmsg, int errmsg_sz)
{
  int n = scandir(d, list, 0, (int (*)(const dirent **, const dirent **))sort);
  if (n < 0 && errmsg) snprintf(errmsg, errmsg_sz, "%s", strerror(errno));
  return n;
}

char *Fl_Headless_System_Driver::preference_rootnode(Fl_Preferences * /*prefs*/, Fl_Preferences::Root root,
                                                     const char *vendor, const char *application)
{
  static char *filename = 0;
  if (!filename) filename = (char *)::calloc(1, FL_PATH_MAX);
  const char *home = ::getenv("HOME");
  if ((root & Fl_Preferences::ROOT_MASK) == Fl_Preferences::SYSTEM || !home) home = "/etc";
  if (!vendor) vendor = "unknown";
  if (!application) application = "unknown";
  snprintf(filename, FL_PATH_MAX, "%s/.config/%s/%s.prefs", home, vendor, application);
  return filename;
}

void Fl_Headless_System_Driver::newUUID(char *uuidBuffer)
{
  unsigned char b[16];
  struct timeval t;
  gettimeofday(&t, NULL);
  srand((unsigned)(t.tv_sec ^ t.tv_usec));
  for (int i = 0; i < 16; i++) b[i] = (unsigned char)(rand() & 0xff);
  b[6] = (unsigned char)((b[6] & 0x0f) | 0x40);
  b[8] = (unsigned char)((b[8] & 0x3f) | 0x80);
  snprintf(uuidBuffer, 36 + 1, "%2.2x%2.2x%2.2x%2.2x-%2.2x%2.2x-%2.2x%2.2x-%2.2x%2.2x-%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x",
           b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}

int Fl_Headless_System_Driver::open_uri(const char *, char *msg, int msglen)
{
  if (msg) snprintf(msg, msglen, "Opening URIs is not supported here");
  return 0;
}

//==== Screen ====//

void Fl_Headless_Screen_Driver::open_display_platform()
{
  // Creates the display's graphics driver and makes it current (fl_graphics_driver), as
  // other platforms do when the display opens; fl_font() etc. need it before any drawing.
  Fl_Display_Device::display_device();
}

void Fl_Headless_Screen_Driver::screen_xywh(int &X, int &Y, int &W, int &H, int)
{
  X = 0; Y = 0; W = 1920; H = 1080;
}

void Fl_Headless_Screen_Driver::screen_work_area(int &X, int &Y, int &W, int &H, int n)
{
  screen_xywh(X, Y, W, H, n);
}

int Fl_Headless_Screen_Driver::get_mouse(int &x, int &y)
{
  x = Fl::e_x_root;
  y = Fl::e_y_root;
  return 0;
}

// Keys held down, as far as the injected events tell: modifiers from the event state, plus
// the key of the current keyboard event.
int Fl_Headless_Screen_Driver::event_key(int k)
{
  switch (k) {
    case FL_Shift_L: case FL_Shift_R: return (Fl::e_state & FL_SHIFT) != 0;
    case FL_Control_L: case FL_Control_R: return (Fl::e_state & FL_CTRL) != 0;
    case FL_Alt_L: case FL_Alt_R: return (Fl::e_state & FL_ALT) != 0;
    case FL_Meta_L: case FL_Meta_R: return (Fl::e_state & FL_META) != 0;
    case FL_Button + 1: return (Fl::e_state & FL_BUTTON1) != 0;
    case FL_Button + 2: return (Fl::e_state & FL_BUTTON2) != 0;
    case FL_Button + 3: return (Fl::e_state & FL_BUTTON3) != 0;
  }
  return (Fl::e_number == FL_KEYDOWN || Fl::e_number == FL_SHORTCUT) && k == Fl::e_keysym;
}

// As X11: text from a keyboard event is inserted unless it's a control character or a
// shortcut (Ctrl/Alt/Meta).
int Fl_Headless_Screen_Driver::compose(int &del)
{
  unsigned char ascii = (unsigned char)Fl::e_text[0];
  if ((Fl::e_state & (FL_ALT | FL_META | FL_CTRL)) && !(ascii & 128)) {
    del = 0;
    return 0;
  }
  del = Fl::compose_state;
  Fl::compose_state = 0;
  if (!(ascii & ~31 && ascii != 127)) return 0;
  return 1;
}

static std::string clipboard_[2];

void Fl_Headless_Screen_Driver::copy(const char *stuff, int len, int clipboard, const char *type)
{
  if (!stuff || len < 0 || strcmp(type, Fl::clipboard_plain_text) != 0) return;
  clipboard_[clipboard ? 1 : 0].assign(stuff, (size_t)len);
}

void Fl_Headless_Screen_Driver::paste(Fl_Widget &receiver, int clipboard, const char *type)
{
  if (strcmp(type, Fl::clipboard_plain_text) != 0) return;
  std::string &s = clipboard_[clipboard ? 1 : 0];
  Fl::e_text = (char *)s.c_str();
  Fl::e_length = (int)s.size();
  Fl::e_clipboard_type = Fl::clipboard_plain_text;
  receiver.handle(FL_PASTE);
}

int Fl_Headless_Screen_Driver::clipboard_contains(const char *type)
{
  return strcmp(type, Fl::clipboard_plain_text) == 0 && !clipboard_[1].empty();
}

//==== Windows: shown and hidden logically, never drawn ====//

void Fl_Headless_Window_Driver::show()
{
  if (shown()) {
    Fl::handle(FL_SHOW, pWindow);
    return;
  }
  fl_open_display();
  static fl_uintptr_t next_xid = 1;
  Fl_X *xp = new Fl_X;
  xp->xid = next_xid++;
  xp->w = pWindow;
  flx(xp);
  xp->region = 0;
  if (!pWindow->parent()) {
    xp->next = Fl_X::first;
    Fl_X::first = xp;
  } else if (Fl_X::first) {
    xp->next = Fl_X::first->next;
    Fl_X::first->next = xp;
  } else {
    xp->next = NULL;
    Fl_X::first = xp;
  }
  if (pWindow->modal()) Fl::modal_ = pWindow;
  size_range();
  pWindow->set_visible();
  int old_event = Fl::e_number;
  pWindow->redraw();
  pWindow->handle(Fl::e_number = FL_SHOW); // get child windows to appear
  Fl::e_number = old_event;
}

void Fl_Headless_Window_Driver::hide()
{
  Fl_X *ip = Fl_X::flx(pWindow);
  if (hide_common()) return;
  delete ip;
}

void Fl_Headless_Window_Driver::resize(int X, int Y, int W, int H)
{
  int is_a_move = (X != x() || Y != y());
  int is_a_resize = (W != w() || H != h());
  if (!is_a_move && !is_a_resize) return;
  if (is_a_resize) {
    pWindow->Fl_Group::resize(X, Y, W, H);
    if (shown()) pWindow->redraw();
  } else {
    x(X);
    y(Y);
  }
}

//==== Graphics: text measurement only ====//

#ifdef __EMSCRIPTEN__
// Measured with the browser's canvas (an OffscreenCanvas, so it also works on worker threads).
EM_JS(double, fl_headless_measure, (const char *font, const char *text, int n), {
  if (!globalThis.__flMeasure) {
    const canvas = typeof OffscreenCanvas !== 'undefined' ? new OffscreenCanvas(1, 1)
                 : (typeof document !== 'undefined' ? document.createElement('canvas') : null);
    globalThis.__flMeasure = canvas ? canvas.getContext('2d') : null;
  }
  const ctx = globalThis.__flMeasure;
  const str = UTF8ToString(text, n);
  if (!ctx) return str.length * 0.55 * parseFloat(UTF8ToString(font).match(/(\d+)px/)[1]);
  ctx.font = UTF8ToString(font);
  return ctx.measureText(str).width;
});
#endif

// A CSS font for an FLTK face and size.
static std::string css_font(Fl_Font face, Fl_Fontsize size)
{
  int f = face < 0 ? 0 : face;
  const char *family = "Arial, Helvetica, sans-serif";
  if ((f >= 4 && f < 8) || f == FL_SCREEN || f == FL_SCREEN_BOLD) family = "'Courier New', Courier, monospace";
  else if (f >= 8 && f < 12) family = "'Times New Roman', Times, serif";
  bool bold = (f < 12 && (f & 1)) || f == FL_SCREEN_BOLD;
  bool italic = f < 12 && (f & 2);
  char buf[128];
  snprintf(buf, sizeof(buf), "%s%s%dpx %s", italic ? "italic " : "", bold ? "bold " : "", (int)size, family);
  return buf;
}

static double measure(Fl_Font face, Fl_Fontsize size, const char *str, int n)
{
#ifdef __EMSCRIPTEN__
  return fl_headless_measure(css_font(face, size).c_str(), str, n);
#else
  // Rough Helvetica-like advance when no canvas is available.
  int chars = 0;
  for (int i = 0; i < n; i++) if ((str[i] & 0xC0) != 0x80) chars++;
  bool mono = (face >= 4 && face < 8) || face == FL_SCREEN || face == FL_SCREEN_BOLD;
  return chars * size * (mono ? 0.6 : 0.55);
#endif
}

void Fl_Headless_Graphics_Driver::font(Fl_Font face, Fl_Fontsize fsize)
{
  Fl_Graphics_Driver::font(face, fsize);
  if (face < 0 || face >= FL_FREE_FONT) face = 0;
  // A descriptor per face and size, as other drivers keep (gl_draw and SVG read it).
  Fl_Fontdesc *s = fl_fonts + face;
  Fl_Font_Descriptor *f;
  for (f = s->first; f; f = f->next) if (f->size == fsize) break;
  if (!f) {
    f = new Fl_Font_Descriptor(s->name, fsize);
    f->ascent = (int)(fsize * 0.91 + 0.5);
    f->descent = (int)(fsize * 0.21 + 0.5);
    f->next = s->first;
    s->first = f;
  }
  font_descriptor(f);
}

double Fl_Headless_Graphics_Driver::width(const char *str, int n)
{
  if (!str || n <= 0) return 0;
  return measure(Fl_Graphics_Driver::font(), size(), str, n);
}

double Fl_Headless_Graphics_Driver::width(unsigned int c)
{
  // Single characters are measured often (text widgets): cache them.
  static std::map<unsigned long long, double> cache;
  unsigned long long key = ((unsigned long long)Fl_Graphics_Driver::font() << 48) ^
                           ((unsigned long long)size() << 32) ^ c;
  auto it = cache.find(key);
  if (it != cache.end()) return it->second;
  char buf[4];
  int n = fl_utf8encode(c, buf);
  double w = measure(Fl_Graphics_Driver::font(), size(), buf, n);
  cache[key] = w;
  return w;
}

int Fl_Headless_Graphics_Driver::height()
{
  return (int)(size() * 1.15 + 0.5);
}

int Fl_Headless_Graphics_Driver::descent()
{
  return (int)(size() * 0.21 + 0.5);
}

const char *Fl_Headless_Graphics_Driver::font_name(int num)
{
  return (num >= 0 && num < FL_FREE_FONT) ? fl_fonts[num].name : NULL;
}

void Fl_Headless_Graphics_Driver::font_name(int num, const char *name)
{
  if (num >= 0 && num < FL_FREE_FONT) fl_fonts[num].name = name;
}

const char *Fl_Headless_Graphics_Driver::get_font_name(Fl_Font fnum, int *ap)
{
  if (ap) *ap = 0;
  return font_name(fnum);
}

//==== Offscreen surfaces: nothing is rasterized ====//

Fl_Headless_Image_Surface_Driver::Fl_Headless_Image_Surface_Driver(int w, int h, int high_res, Fl_Offscreen off)
  : Fl_Image_Surface_Driver(w, h, high_res, off)
{
  driver(new Fl_Headless_Graphics_Driver());
}

Fl_Headless_Image_Surface_Driver::~Fl_Headless_Image_Surface_Driver()
{
  delete driver();
}

void Fl_Headless_Image_Surface_Driver::set_current()
{
  Fl_Surface_Device::set_current();
}

Fl_RGB_Image *Fl_Headless_Image_Surface_Driver::image()
{
  int w = width > 0 ? width : 1, h = height > 0 ? height : 1;
  uchar *data = new uchar[(size_t)w * h * 3];
  memset(data, 255, (size_t)w * h * 3);
  Fl_RGB_Image *img = new Fl_RGB_Image(data, w, h, 3);
  img->alloc_array = 1;
  return img;
}

Fl_Headless_Copy_Surface_Driver::Fl_Headless_Copy_Surface_Driver(int w, int h)
  : Fl_Copy_Surface_Driver(w, h)
{
  driver(new Fl_Headless_Graphics_Driver());
}

Fl_Headless_Copy_Surface_Driver::~Fl_Headless_Copy_Surface_Driver()
{
  delete driver();
}

void Fl_Headless_Copy_Surface_Driver::set_current()
{
  Fl_Surface_Device::set_current();
}
