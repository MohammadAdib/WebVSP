//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

// The operating system's own open/save dialog, for the web GUI: the engine runs on the
// user's machine, so the file pickers it shows are the desktop's. Callable from any thread
// (the web backend calls it off the OpenVSP thread so the GUI keeps running).
//
//////////////////////////////////////////////////////////////////////

#if !defined( NATIVEFILEDIALOG_H_ )
#define NATIVEFILEDIALOG_H_

#include <string>

namespace NativeFileDialog
{

// Shows the dialog in front of whatever window is active and blocks until it closes.
//   types:  one "Label\tpattern" per line, patterns separated by ';' (e.g. "Images\t*.png;*.jpg")
//   dir:    folder to start in (may be empty)
//   file:   suggested file name (may be empty)
//   defext: extension added to a typed name without one, e.g. "vsp3" (save only)
// Returns the chosen path (UTF-8), "" if cancelled, or "*error <reason>" if no dialog could
// be shown (e.g. on a platform without one).
std::string Show( const std::string &title, bool save, const std::string &types, const std::string &dir,
                  const std::string &file, const std::string &defext );

}

#endif // NATIVEFILEDIALOG_H_
