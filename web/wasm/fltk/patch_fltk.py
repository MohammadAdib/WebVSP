"""Add the "headless" platform (FLTK without a window system) to an extracted FLTK 1.4 tree.

    python patch_fltk.py <fltk source dir>

Copies the platform's files (FL/headless.H, src/drivers/Headless/*) and makes the small
CMake/header edits that select it with -DFLTK_BACKEND_HEADLESS=ON. Used by
web/wasm/build_wasm.ps1 to build FLTK for WebVSP's in-browser engine; idempotent.
"""

import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
MARK = "FLTK_BACKEND_HEADLESS"


def edit(path: Path, old: str, new: str) -> None:
    raw = path.read_bytes().decode()
    nl = "\r\n" if "\r\n" in raw else "\n"
    text = raw.replace("\r\n", "\n")
    if new in text:
        return  # already patched
    if text.count(old) != 1:
        raise SystemExit(f"{path}: expected exactly one match for:\n{old}")
    path.write_bytes(text.replace(old, new).replace("\n", nl).encode())


def main(src: Path) -> None:
    # The platform's own files.
    shutil.copy2(HERE / "FL" / "headless.H", src / "FL" / "headless.H")
    drivers = src / "src" / "drivers" / "Headless"
    drivers.mkdir(parents=True, exist_ok=True)
    for f in (HERE / "src" / "drivers" / "Headless").iterdir():
        shutil.copy2(f, drivers / f.name)

    # <FL/platform.H> picks the platform header.
    edit(src / "FL" / "platform.H",
         '#  ifdef _WIN32\n#    include "win32.H"\n',
         '#  if defined(FLTK_USE_HEADLESS)\n#    include "headless.H"\n'
         '#  elif defined(_WIN32)\n#    include "win32.H"\n')

    # FL/fl_config.h gets FLTK_USE_HEADLESS.
    edit(src / "fl_config.cmake.in",
         "#cmakedefine FLTK_USE_WAYLAND 1\n",
         "#cmakedefine FLTK_USE_WAYLAND 1\n\n\n"
         "/*\n * FLTK_USE_HEADLESS\n *\n * Do we use the headless platform (no window system)?\n *\n */\n\n"
         "#cmakedefine FLTK_USE_HEADLESS 1\n")

    options = src / "CMake" / "options.cmake"
    # The option itself; it rules out the other backends.
    edit(options,
         "#######################################################################\n"
         "#  Bundled Library Options\n",
         f'option({MARK} "headless platform: no window system (e.g. WebAssembly)" OFF)\n'
         f"if({MARK})\n"
         "  set(FLTK_USE_HEADLESS 1)\n"
         "  set(FLTK_BACKEND_X11 OFF CACHE BOOL \"\" FORCE)\n"
         "  set(FLTK_BACKEND_WAYLAND OFF CACHE BOOL \"\" FORCE)\n"
         f"endif({MARK})\n\n"
         "#######################################################################\n"
         "#  Bundled Library Options\n")
    # No X11 lookup.
    edit(options,
         "if((NOT APPLE OR FLTK_BACKEND_X11) AND NOT WIN32 AND NOT FLTK_BACKEND_WAYLAND)\n  include(FindX11)",
         f"if((NOT APPLE OR FLTK_BACKEND_X11) AND NOT WIN32 AND NOT FLTK_BACKEND_WAYLAND AND NOT {MARK})\n  include(FindX11)")
    # OpenGL: windows exist but have no context, so there's nothing to find or link.
    edit(options,
         "set(HAVE_GL LIB_GL OR LIB_MesaGL)\n",
         f"if({MARK})\n  set(HAVE_GL TRUE)\nelse()\n  set(HAVE_GL LIB_GL OR LIB_MesaGL)\nendif()\n")
    edit(options,
         "  if(FLTK_BACKEND_X11)\n    set(OPENGL_FOUND TRUE)\n",
         f"  if({MARK})\n    set(OPENGL_FOUND TRUE)\n    set(OPENGL_LIBRARIES)\n    set(HAVE_GL_GLU_H TRUE)\n"
         "  elseif(FLTK_BACKEND_X11)\n    set(OPENGL_FOUND TRUE)\n")
    edit(options,
         "  endif(FLTK_BACKEND_X11)\nelse(FLTK_BUILD_GL)\n",
         "  endif()\nelse(FLTK_BUILD_GL)\n")
    # No printing.
    edit(options,
         "if(X11_FOUND AND NOT FLTK_OPTION_PRINT_SUPPORT)\n  set(FL_NO_PRINT_SUPPORT TRUE)\n"
         "endif(X11_FOUND AND NOT FLTK_OPTION_PRINT_SUPPORT)\n",
         f"if((X11_FOUND OR {MARK}) AND NOT FLTK_OPTION_PRINT_SUPPORT)\n  set(FL_NO_PRINT_SUPPORT TRUE)\nendif()\n")

    cmake = src / "src" / "CMakeLists.txt"
    edit(cmake,
         "if(FLTK_USE_X11 AND NOT FLTK_OPTION_PRINT_SUPPORT)\n  set(PSFILES)\n",
         f"if((FLTK_USE_X11 OR {MARK}) AND NOT FLTK_OPTION_PRINT_SUPPORT)\n  set(PSFILES)\n")
    edit(cmake,
         "endif(FLTK_USE_X11 AND NOT FLTK_OPTION_PRINT_SUPPORT)\n",
         "endif()\n")
    # The platform's sources (before the final else(), which is the Windows list).
    edit(cmake,
         "else()\n\n  # Windows (GDI)\n",
         f"elseif({MARK})\n\n"
         "  # Headless: no window system (WebVSP's in-browser engine, web/wasm/fltk)\n\n"
         "  set(DRIVER_FILES\n"
         "    drivers/Posix/Fl_Posix_System_Driver.cxx\n"
         "    drivers/Headless/fl_headless_platform_init.cxx\n"
         "    Fl_Native_File_Chooser_FLTK.cxx\n"
         "  )\n"
         "  set(DRIVER_HEADER_FILES\n"
         "    drivers/Posix/Fl_Posix_System_Driver.H\n"
         "    drivers/Headless/Fl_Headless_Drivers.H\n"
         "  )\n\n"
         "else()\n\n  # Windows (GDI)\n")
    edit(cmake,
         "if(FLTK_BACKEND_WAYLAND)\n  set(GL_DRIVER_FILES ${GL_DRIVER_FILES} drivers/Wayland/Fl_Wayland_Gl_Window_Driver.cxx)\n",
         f"if({MARK})\n  set(GL_DRIVER_FILES ${{GL_DRIVER_FILES}} drivers/Headless/Fl_Headless_Gl_Window_Driver.cxx)\n"
         "elseif(FLTK_BACKEND_WAYLAND)\n  set(GL_DRIVER_FILES ${GL_DRIVER_FILES} drivers/Wayland/Fl_Wayland_Gl_Window_Driver.cxx)\n")
    edit(cmake,
         "  set(GL_DRIVER_HEADER_FILES drivers/WinAPI/Fl_WinAPI_Gl_Window_Driver.H)\nendif(FLTK_BACKEND_WAYLAND)\n",
         "  set(GL_DRIVER_HEADER_FILES drivers/WinAPI/Fl_WinAPI_Gl_Window_Driver.H)\nendif()\n")
    print(f"FLTK at {src}: headless platform added")


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve())
