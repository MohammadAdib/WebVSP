//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

// Modal prompts and waits used by the GUI.  Normally these are plain FLTK calls; when the
// GUI is mirrored to a web client (see WebMirror.h) they are answered by that client.
//
//////////////////////////////////////////////////////////////////////

#ifndef VSPDIALOGS_H_
#define VSPDIALOGS_H_

#include <string>

// Same contract as fl_choice: returns the index (0..2) of the button pressed.
int VspChoice( const char *fmt, const char *b0, const char *b1, const char *b2, ... );

// Same contract as fl_alert / fl_message.
void VspAlert( const char *fmt, ... );
void VspMessage( const char *fmt, ... );

// One iteration of a screen's local modal loop (replaces a bare Fl::wait()).
void VspModalWait();

#endif // VSPDIALOGS_H_
